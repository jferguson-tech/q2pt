# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Recording play, keeping it on disk, and cutting it into training batches.

A dataset is a folder of shards under ~/q2pt-rl/data/. A shard is an .npz
of whole episodes laid end to end, with:

    self, guide, ray_dist, ray_slope, ray_kind, ents    what was perceived
    teacher     the teacher's action for that state
    action      the action that was taken, whoever took it
    start       1 at the first step of an episode
    gain        the parts of the reward

The rays are stored small (distance in sixteenths of a unit as 16 bit, slope
as 8 bit) and the things in view as 16 bit floats: about 1.4 kB a step.
"""

import json
import time
from pathlib import Path

import numpy as np
import torch

from q2env import Q2VecEnv, layout as L
from q2env.engine import data_dir

# faster-clocked cores first; cores 0 and 1 are left for the trainer and the system
CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))

STORE = {
    "self": lambda a: a.astype(np.float32),
    "guide": lambda a: a.astype(np.float32),
    "ray_dist": lambda a: np.clip(a * 16, 0, 65535).astype(np.uint16),
    "ray_slope": lambda a: np.clip(a * 127, -127, 127).astype(np.int8),
    "ray_kind": lambda a: a.astype(np.uint8),
    "ents": lambda a: a.astype(np.float16),
}
LOAD = {
    "ray_dist": lambda a: a.astype(np.float32) / 16,
    "ray_slope": lambda a: a.astype(np.float32) / 127,
}


def to_torch(obs, device):
    """A dict of observation arrays as stored or as the environment gives
    them, as float or integer tensors on the device."""
    out = {}
    for k in L.OBS_FIELDS:
        a = obs[k]
        if isinstance(a, np.ndarray):
            a = torch.from_numpy(np.ascontiguousarray(a))
        a = a.to(device, non_blocking=True)
        if k == "ray_dist" and a.dtype == torch.uint16:
            a = a.float() / 16
        elif k == "ray_slope" and a.dtype == torch.int8:
            a = a.float() / 127
        out[k] = a
    return out


def collect(policy_step, episodes, maps, out_dir, seed, time_limit=6000, skill=1, envs=28,
            shard_episodes=64, mode=L.MODE_PLAY, back=None, on_taken=None):
    """Plays whole episodes and writes them as shards.

    policy_step(obs, starts) is called once per step with the batch of
    observations and a mask of the environments that have just been reset. It
    returns (actions, use_teacher): the actions to take, and a mask of the
    environments in which the teacher's own action is played instead.
    actions may be None when the teacher plays everywhere. on_taken, if
    given, is called with the actions that were played.

    Returns what the game reported at the end of every episode."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    env = Q2VecEnv(envs, maps=maps, skill=skill, time_limit=time_limit, cpus=CPUS,
                   seed=seed, mode=mode, back=back)
    obs = env.reset()
    starts = np.ones(envs, bool)
    live = [[] for _ in range(envs)]        # the steps of each environment's episode so far
    done_eps, results, shard = [], [], len(list(out_dir.glob("shard_*.npz")))

    def flush(force=False):
        nonlocal shard, done_eps
        while len(done_eps) >= shard_episodes or (force and done_eps):
            batch, done_eps = done_eps[:shard_episodes], done_eps[shard_episodes:]
            arrays = {k: np.concatenate([e[k] for e in batch]) for k in batch[0]}
            np.savez(out_dir / f"shard_{shard:05d}.npz", **arrays)
            shard += 1

    while len(results) < episodes:
        actions, use_teacher = policy_step(obs, starts)
        teacher = env.teacher.copy()
        if actions is None:
            taken = teacher
        else:
            taken = np.where(use_teacher[:, None], teacher, actions)
        if on_taken:
            on_taken(taken)
        row = {k: STORE[k](obs[k]) for k in L.OBS_FIELDS}
        for i in range(envs):
            live[i].append({**{k: row[k][i] for k in row}, "teacher": teacher[i].astype(np.uint8),
                            "action": taken[i].astype(np.uint8), "start": starts[i]})
        obs, gain, done = env.step(taken)
        for i in range(envs):
            live[i][-1]["gain"] = gain[i].copy()
            if done[i]:
                steps = live[i]
                done_eps.append({k: np.stack([s[k] for s in steps]) for k in steps[0]})
                results.append(env.info[i])
                live[i] = []
        starts = done != L.DONE_NO
        flush()
    flush(True)
    env.close()
    (out_dir / "episodes.json").write_text(json.dumps(results))
    return results


class Dataset:
    """Shards held in memory, cut into fixed lengths for truncated
    backpropagation through time."""

    def __init__(self, dirs, length=64):
        self.length = length
        parts = {}
        for d in dirs:
            for f in sorted(Path(d).glob("shard_*.npz")):
                with np.load(f) as z:
                    for k in z.files:
                        parts.setdefault(k, []).append(z[k])
        self.data = {k: np.concatenate(v) for k, v in parts.items()}
        self.steps = len(self.data["start"])
        # the action taken at the step before, which the policy is given;
        # the idle action at the start of an episode
        last = np.roll(self.data["action"], 1, axis=0)
        last[self.data["start"].astype(bool)] = L.ACT_IDLE
        self.data["last"] = last
        self.windows = self.steps // length

    def batches(self, batch, device, rng):
        """Yields dicts of tensors (batch, length, ...), every window once in
        a shuffled order. A window may run from one episode into the next:
        `start` marks where, and the network's state is cleared there."""
        order = rng.permutation(self.windows)
        for i in range(0, len(order) - batch + 1, batch):
            idx = (order[i:i + batch, None] * self.length + np.arange(self.length)[None]).reshape(-1)
            out = {}
            for k, a in self.data.items():
                t = torch.from_numpy(a[idx].reshape(batch, self.length, *a.shape[1:])).to(device)
                out[k] = t
            yield out


def size_gb(path=None):
    """How much is on disk under the data folder, in GB."""
    root = Path(path) if path else data_dir()
    return sum(f.stat().st_size for f in root.rglob("*") if f.is_file()) / 1e9


def now():
    return time.strftime("%Y-%m-%d %H:%M:%S")
