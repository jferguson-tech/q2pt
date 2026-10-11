# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Which maps test anything: where a player with no sense ends up.

    python rl/tools/walker.py [--episodes 14] [--steps 3000] [map ...]

Two players are tried: one that only ever walks forward, and one that takes
an action at random every step. Each is put into a map at every place the
game brings the player into it: the spawn point each exit of another map
names, and the plain start where no exit names one. For each such arrival
the table says which exits the two players left by, and how often.

An episode counts only when it ends at the exit that leads on. An arrival is
a real test of a player when these two do not reach that exit; they often
reach the one beside them, which leads back where they came from. For the
legs of the units in q2env/maps.py the exit that leads on is known and is
marked with a star. With no maps named, every single-player map is tried.
"""

import argparse
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402
from q2env.bsp import entities, exits, leads_to  # noqa: E402
from q2env.maps import LEGS, ORDER  # noqa: E402

CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))


def arrivals(maps):
    """For each map, the spawn points the exits of all the maps name in it;
    "" is the start that has no name."""
    into = {m: set() for m in maps}
    for m in ORDER:
        for exit in exits(m):
            to, spawn = leads_to(exit)
            if to in into:
                into[to].add(spawn)
    for m in maps:
        named = {e.get("targetname", "").lower() for e in entities(m) if e.get("classname") == "info_player_start"}
        into[m] = sorted(s for s in into[m] if s in named) or [""]
        if m == ORDER[0] and "" not in into[m]:
            into[m].insert(0, "")
    return into


def play(start, seed, steps, random, cpu):
    rng = np.random.default_rng(seed)
    forward = np.array(L.ACT_IDLE)
    forward[0] = 2
    with Engine(cpu=cpu) as e:
        e.reset(start, seed, time_limit=steps, mode=L.MODE_PLAY, back="")
        b = e.block
        while not b["done"]:
            e.step(np.array([rng.integers(n) for n in L.ACT_SIZES]) if random else forward)
        return b["left"].decode(), int(b["step"])


def onward(map, spawn):
    for start, exit, _ in LEGS.values():
        if start in (f"{map}${spawn}", map if not spawn else None):
            return exit
    return None


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--episodes", type=int, default=14)
    ap.add_argument("--steps", type=int, default=3000)
    args = ap.parse_args()

    maps = args.maps or ORDER
    print("| map | arriving from | exits | walking forward left by | acting at random left by |")
    print("| --- | --- | --- | --- | --- |")
    with ThreadPoolExecutor(len(CPUS)) as pool:
        for map, spawns in arrivals(maps).items():
            for spawn in spawns:
                on = onward(map, spawn)
                start = f"{map}${spawn}" if spawn else map
                cells = []
                for random in (False, True):
                    jobs = [pool.submit(play, start, seed, args.steps, random, CPUS[seed % len(CPUS)])
                            for seed in range(args.episodes)]
                    ends = [j.result() for j in jobs]
                    left = Counter(where for where, _ in ends if where)
                    cells.append(", ".join(
                        f"{'*' if where == on else ''}{where} {n} "
                        f"({np.median([s for w, s in ends if w == where]) / 10:.0f} s)"
                        for where, n in left.most_common()) or "none")
                print(f"| {map} | {spawn or 'the start'} | {', '.join(('*' if x == on else '') + x for x in exits(map))} "
                      f"| {cells[0]} | {cells[1]} |", flush=True)
