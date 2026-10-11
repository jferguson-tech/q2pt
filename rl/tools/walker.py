# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Which maps test anything: how often a player with no sense reaches an exit.

    python rl/tools/walker.py [--episodes 14] [--steps 3000] [map ...]

Two players are tried on each map: one that only ever walks forward, and one
that takes an action at random every step. In a hub of the game the player
arrives beside the way back, and on such a map either of these "finishes".
A map where they do is no test of a player that is meant to find and fight
its way out. With no maps named, every single-player map is tried.
"""

import argparse
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402
from q2env.maps import ORDER, back_of  # noqa: E402

CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))


def play(map, seed, steps, random, cpu):
    rng = np.random.default_rng(seed)
    forward = np.array(L.ACT_IDLE)
    forward[0] = 2
    with Engine(cpu=cpu) as e:
        e.reset(map, seed, time_limit=steps, mode=L.MODE_PLAY, back=back_of(map))
        b = e.block
        while not b["done"]:
            e.step(np.array([rng.integers(n) for n in L.ACT_SIZES]) if random else forward)
        return int(b["done"]), int(b["step"])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--episodes", type=int, default=14)
    ap.add_argument("--steps", type=int, default=3000)
    args = ap.parse_args()

    print("| map | walking forward | acting at random |")
    print("| --- | --- | --- |")
    with ThreadPoolExecutor(len(CPUS)) as pool:
        for map in args.maps or ORDER:
            cells = []
            for random in (False, True):
                jobs = [pool.submit(play, map, seed, args.steps, random, CPUS[seed % len(CPUS)])
                        for seed in range(args.episodes)]
                ends = [j.result() for j in jobs]
                won = [s for d, s in ends if d == L.DONE_EXIT]
                cells.append(f"{len(won)} of {len(ends)}" + (f", {np.median(won) / 10:.0f} s" if won else ""))
            print(f"| {map} | {cells[0]} | {cells[1]} |", flush=True)
