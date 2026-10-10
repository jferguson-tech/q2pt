# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The Gymnasium environment and a vector of them.

The observation is the block's own fields, unscaled: the player's state, the
guide (zeros for the player that is not told the way), the rays and the
things in view. Scaling is the policy's business.

`info` carries the teacher's action for the state returned, the parts the
reward is made of, how the episode ended, and the player's true place, which
is for scoring and never for the policy.
"""

import gymnasium as gym
import numpy as np

from . import layout as L
from .engine import Engine


def observation_space():
    f = np.float32
    return gym.spaces.Dict({
        "self": gym.spaces.Box(-np.inf, np.inf, (L.SELF_FLOATS,), f),
        "guide": gym.spaces.Box(-np.inf, np.inf, (L.GUIDE_FLOATS,), f),
        "ray_dist": gym.spaces.Box(0, L.RAY_RANGE, (L.RAYS,), f),
        "ray_slope": gym.spaces.Box(-1, 1, (L.RAYS,), f),
        "ray_kind": gym.spaces.Box(0, L.HIT_KINDS - 1, (L.RAYS,), np.int32),
        "ents": gym.spaces.Box(-np.inf, np.inf, (L.ENTS, L.ENT_FLOATS), f),
    })


def action_space():
    return gym.spaces.MultiDiscrete(L.ACT_SIZES)


def _obs(block, guided):
    obs = {k: np.array(block[k]) for k in L.OBS_FIELDS}
    if not guided:
        obs["guide"][:] = 0
    return obs


def _info(block):
    return {
        "teacher": np.array(block["teacher"]),
        "gain": np.array(block["gain"]),
        "done": int(block["done"]),
        "hash": int(block["hash"]),
        "origin": np.array(block["origin"]),
        "angles": np.array(block["angles"]),
        "monsters_killed": int(block["monsters_killed"]),
        "monsters_total": int(block["monsters_total"]),
    }


class Q2Env(gym.Env):
    """One player in one map.

    The reward returned is zero: it is made outside, from info["gain"], so
    that one recording can be scored in more than one way.
    """

    metadata = {"render_modes": []}

    def __init__(self, map="base1", skill=1, time_limit=3000, guided=True, cpu=None, log=None):
        self.map, self.skill, self.time_limit, self.guided = map, skill, time_limit, guided
        self.observation_space = observation_space()
        self.action_space = action_space()
        self.engine = Engine(cpu=cpu, log=log)

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        if seed is None:
            seed = int(self.np_random.integers(1 << 31))
        options = options or {}
        self.engine.reset(options.get("map", self.map), seed,
                          options.get("skill", self.skill),
                          options.get("time_limit", self.time_limit),
                          options.get("demo", ""))
        b = self.engine.block
        return _obs(b, self.guided), _info(b)

    def step(self, action):
        """action: an array of one choice per branch, or None to let the
        teacher act."""
        self.engine.step(action)
        b = self.engine.block
        done = int(b["done"])
        return (_obs(b, self.guided), 0.0, done in (L.DONE_EXIT, L.DONE_DEATH),
                done == L.DONE_TIME, _info(b))

    def close(self):
        self.engine.close()


class Q2VecEnv:
    """Many players, each in a server process of its own, stepped together.

    All the servers are set going before any is waited for, so they run side
    by side on their own processor threads. An environment whose episode has
    ended is reset on the next step, with the next seed of its own sequence;
    that step returns the first observation of the new episode and the action
    given for it is not used.
    """

    def __init__(self, n, maps=("base1",), skill=1, time_limit=3000, guided=True,
                 cpus=None, seed=0):
        self.n = n
        self.maps = tuple(maps)
        self.skill, self.time_limit, self.guided = skill, time_limit, guided
        self.single_observation_space = observation_space()
        self.single_action_space = action_space()
        self.engines = [Engine(cpu=cpus[i % len(cpus)] if cpus else None) for i in range(n)]
        self._seed = seed
        self._episodes = np.zeros(n, np.int64)
        self._need_reset = np.ones(n, bool)
        self._view = np.concatenate([e.block.reshape(1) for e in self.engines])  # for dtypes only
        self.obs = {k: np.zeros((n,) + L.SHARED[k].shape, L.SHARED[k].base) for k in L.OBS_FIELDS}
        self.teacher = np.zeros((n, L.ACT_BRANCHES), np.int32)
        self.gain = np.zeros((n, L.GAIN_FLOATS), np.float32)
        self.done = np.zeros(n, np.int32)
        self.hash = np.zeros(n, np.uint32)

    def _ask_reset(self, i):
        # each environment has its own run of seeds and takes the maps in turn
        k = int(self._episodes[i])
        self.engines[i].ask_reset(self.maps[(i + k) % len(self.maps)],
                                  (self._seed + i * 1000003 + k) & 0x7FFFFFFF,
                                  self.skill, self.time_limit)
        self._episodes[i] += 1

    def _collect(self):
        for i, e in enumerate(self.engines):
            e.wait()
            b = e.block
            for k in L.OBS_FIELDS:
                self.obs[k][i] = b[k]
            self.teacher[i] = b["teacher"]
            self.gain[i] = b["gain"]
            self.done[i] = b["done"]
            self.hash[i] = b["hash"]
        if not self.guided:
            self.obs["guide"][:] = 0
        self._need_reset = self.done != L.DONE_NO

    def reset(self):
        for i in range(self.n):
            self._ask_reset(i)
        self._collect()
        return self.obs

    def step(self, actions=None, teacher=None):
        """actions: (n, branches) choices. teacher: None, or a mask of the
        environments in which the teacher's own action is played instead.

        Returns the observations, the gains, how each episode stands (DONE_),
        and which environments were reset in this call."""
        was_reset = self._need_reset.copy()
        for i, e in enumerate(self.engines):
            if was_reset[i]:
                self._ask_reset(i)
            elif teacher is not None and teacher[i]:
                e.ask_step(None)
            else:
                e.ask_step(actions[i])
        self._collect()
        return self.obs, self.gain, self.done, was_reset

    def close(self):
        for e in self.engines:
            e.close()
