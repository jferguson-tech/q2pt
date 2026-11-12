# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Steps per second with random actions.

    python rl/tools/bench.py one <thread> [seconds]     one server on one thread
    python rl/tools/bench.py many <threads> [seconds]   one server on each, together

<threads> is a list such as 0-7,16-23. The player moves forward most of the
time, so that the run leaves the start and meets what the map holds; episodes
end at 1,000 steps or at death and the reset is part of what is timed.
"""

import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, Q2VecEnv, layout as L  # noqa: E402


def threads(text):
    out = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        out += range(int(a), int(b or a) + 1)
    return out


def random_actions(rng, n):
    a = np.stack([rng.integers(k, size=n) for k in L.ACT_SIZES], axis=1).astype(np.int32)
    a[:, L.ACT_FORWARD] = np.where(rng.random(n) < 0.7, 2, a[:, L.ACT_FORWARD])
    return a


def one(cpu, seconds, map="base1"):
    rng = np.random.default_rng(0)
    acts = random_actions(rng, 100000)
    steps = resets = 0
    with Engine(cpu=cpu) as e:
        e.reset(map, 0, time_limit=1000)
        start = time.perf_counter()
        while time.perf_counter() - start < seconds:
            for a in acts[steps % 90000: steps % 90000 + 1000]:
                if e.block["done"]:
                    resets += 1
                    e.reset(map, resets, time_limit=1000)
                e.step(a)
                steps += 1
        spent = time.perf_counter() - start
    return steps / spent, resets


def many(cpus, seconds, maps=("base1",)):
    rng = np.random.default_rng(0)
    n = len(cpus)
    env = Q2VecEnv(n, maps=maps, time_limit=1000, cpus=cpus)
    env.reset()
    acts = random_actions(rng, 4096 * n).reshape(4096, n, -1)
    steps = 0
    start = time.perf_counter()
    while time.perf_counter() - start < seconds:
        env.step(acts[steps % 4096])
        steps += 1
    spent = time.perf_counter() - start
    env.close()
    return steps * n / spent


if __name__ == "__main__":
    mode = sys.argv[1]
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 10
    if mode == "one":
        sps, resets = one(int(sys.argv[2]), seconds)
        print(f"thread {sys.argv[2]}: {sps:.0f} steps/s, {resets} resets")
    else:
        cpus = threads(sys.argv[2])
        sps = many(cpus, seconds)
        print(f"{len(cpus)} servers on threads {sys.argv[2]}: {sps:.0f} steps/s in total, {sps / len(cpus):.0f} each")
