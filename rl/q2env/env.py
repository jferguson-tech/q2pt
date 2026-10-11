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
from .maps import back_of


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
    by side on their own processor threads. When an episode ends, its
    environment is reset within the same call to step: the call reports how
    the episode ended, and the observation it returns for that environment is
    the first of the next episode, which takes the next seed of the
    environment's own sequence.
    """

    def __init__(self, n, maps=("base1",), skill=1, time_limit=3000, guided=True,
                 cpus=None, seed=0, mode=L.MODE_EXPLORE, flags=0, back=None):
        """mode, flags: as Engine.reset takes them. back: for each map, the
        maps whose exits are not the way on; None for those that come before
        it in the game."""
        self.n = n
        self.maps = tuple(maps)
        self.skill, self.time_limit, self.guided = skill, time_limit, guided
        self.mode, self.flags = mode, flags
        self.back = {m: back_of(m) for m in self.maps} if back is None else back
        self.current_map = [""] * n
        self.current_seed = np.zeros(n, np.int64)
        self.single_observation_space = observation_space()
        self.single_action_space = action_space()
        self.engines = [Engine(cpu=cpus[i % len(cpus)] if cpus else None) for i in range(n)]
        self._seed = seed
        self._episodes = np.zeros(n, np.int64)
        self.info = [None] * n
        self.obs = {k: np.zeros((n,) + L.SHARED[k].shape, L.SHARED[k].base) for k in L.OBS_FIELDS}
        self.teacher = np.zeros((n, L.ACT_BRANCHES), np.int32)
        self.gain = np.zeros((n, L.GAIN_FLOATS), np.float32)
        self.done = np.zeros(n, np.int32)
        self.hash = np.zeros(n, np.uint32)

    def _ask_reset(self, i):
        # each environment has its own run of seeds and takes the maps in turn
        k = int(self._episodes[i])
        map = self.maps[(i + k) % len(self.maps)]
        seed = (self._seed + i * 1000003 + k) & 0x7FFFFFFF
        self.engines[i].ask_reset(map, seed, self.skill, self.time_limit, flags=self.flags,
                                  mode=self.mode, back=self.back.get(map, ""))
        self.current_map[i] = map
        self.current_seed[i] = seed
        self._episodes[i] += 1

    def _read(self, i):
        b = self.engines[i].block
        for k in L.OBS_FIELDS:
            self.obs[k][i] = b[k]
        self.teacher[i] = b["teacher"]

    def reset(self):
        for i in range(self.n):
            self._ask_reset(i)
        for i, e in enumerate(self.engines):
            e.wait()
            self._read(i)
        if not self.guided:
            self.obs["guide"][:] = 0
        return self.obs

    def step(self, actions=None, teacher=None):
        """actions: (n, branches) choices. teacher: None, or a mask of the
        environments in which the teacher's own action is played instead;
        with actions None the teacher plays in all of them.

        Returns the observations, the gains of the step, and how each
        episode stands (DONE_). Where that is not DONE_NO the observation is
        the first of a new episode. self.teacher holds the teacher's action
        for each observation returned, and self.info what the game reported
        at the end of each episode that ended in this call."""
        for i, e in enumerate(self.engines):
            if actions is None or (teacher is not None and teacher[i]):
                e.ask_step(None)
            else:
                e.ask_step(actions[i])
        ended = []
        for i, e in enumerate(self.engines):
            e.wait()
            b = e.block
            self.gain[i] = b["gain"]
            self.done[i] = b["done"]
            self.hash[i] = b["hash"]
            if b["done"]:
                ended.append(i)
                self.info[i] = {"map": self.current_map[i], "seed": int(self.current_seed[i]),
                                "done": int(b["done"]), "steps": int(b["step"]),
                                "kills": int(b["monsters_killed"]), "health": float(b["self"][0]),
                                "death_by": int(b["death_by"]), "death_means": int(b["death_means"])}
                self._ask_reset(i)
            else:
                self._read(i)
        for i in ended:
            self.engines[i].wait()
            self._read(i)
        if not self.guided:
            self.obs["guide"][:] = 0
        return self.obs, self.gain, self.done

    def close(self):
        for e in self.engines:
            e.close()
