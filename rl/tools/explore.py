# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""How much of each map the explorer covers.

    python rl/tools/explore.py [--episodes 8] [--steps 3000] [--monsters] [map ...]

The teacher's explorer walks to one node of the navigation graph after
another, each drawn from the episode's seed. For each map this plays some
episodes with the teacher driving and prints: the size of the graph, how many
goals were reached and how many given up for taking too long, and the share
of the graph's nodes the player was at in any of the episodes.

Without --monsters the map's monsters are taken out first, so that what is
measured is the finding of the way and nothing else. With no maps named,
every single-player map of the game is played.
"""

import argparse
import sys
from collections import Counter
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, EngineError, layout as L  # noqa: E402
from q2env.nav import Nav, LINK_NAMES  # noqa: E402

SINGLE_PLAYER = """base1 base2 base3 train bunk1 ware1 ware2 jail1 jail2 jail3 jail4 jail5
security mintro mine1 mine2 mine3 mine4 fact1 fact2 fact3 power1 power2 cool1 waste1
waste2 waste3 biggun hangar1 hangar2 lab command strike space city1 city2 city3 boss1
boss2""".split()

# faster-clocked cores first, cores 0 and 1 left free
CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))


def cause(trail):
    """What the player was doing in the last steps before a goal was given
    up: the kind of link it was on, whether that hung on a door or lift, and
    whether it was standing still or going round."""
    if not trail:
        return "at once"
    origin, kind, ent = trail[-1]
    if kind < 0:
        return "no way"
    span = np.ptp(np.stack([t[0] for t in trail]), axis=0).max()
    how = "still" if span < 8 else "to and fro" if span < 100 else "moving"
    return f"{LINK_NAMES[kind]}{' mover' if ent else ''}, {how}"


def play(map, episodes, steps, flags, cpu):
    out = {"map": map, "reached": 0, "failed": 0, "deaths": 0, "steps": 0, "causes": Counter()}
    try:
        with Engine(cpu=cpu) as e:
            start = time.perf_counter()
            e.reset(map, 0, time_limit=steps, flags=flags)
            out["build_s"] = time.perf_counter() - start
            b = e.block
            seen = np.zeros(int(b["nav_count"]), bool)
            for episode in range(episodes):
                if episode:
                    e.reset(map, episode, time_limit=steps, flags=flags)
                failed = 0
                trail = []
                while not b["done"]:
                    e.step(None)
                    if b["nav_node"] >= 0:
                        seen[b["nav_node"]] = True
                    trail.append((b["origin"].copy(), int(b["link_type"]), int(b["link_ent"])))
                    if b["goals_failed"] > failed:
                        failed = int(b["goals_failed"])
                        out["causes"][cause(trail[-40:-1])] += 1
                out["reached"] += int(b["goals_reached"])
                out["failed"] += int(b["goals_failed"])
                out["deaths"] += int(b["done"] == L.DONE_DEATH)
                out["steps"] += int(b["step"])
        nav = Nav(map)
        out["nodes"] = len(nav.nodes)
        out["links"] = {LINK_NAMES[t]: int((nav.links["type"] == t).sum()) for t in range(6)}
        out["visited"] = float(seen.mean())
    except (EngineError, OSError, ValueError) as err:
        out["error"] = str(err)
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--episodes", type=int, default=8)
    ap.add_argument("--steps", type=int, default=3000)
    ap.add_argument("--monsters", action="store_true")
    args = ap.parse_args()
    maps = args.maps or SINGLE_PLAYER
    flags = 0 if args.monsters else L.FLAG_NOMONSTERS

    print("| map | nodes | walk | jump | duck | swim | climb | ride | goals reached | given up | nodes visited |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    with ThreadPoolExecutor(len(CPUS)) as pool:
        jobs = [pool.submit(play, m, args.episodes, args.steps, flags, CPUS[i % len(CPUS)])
                for i, m in enumerate(maps)]
        total = [0, 0]
        for job in jobs:
            r = job.result()
            if "error" in r:
                print(f"| {r['map']} | failed: {r['error']} |")
                continue
            k = r["links"]
            goals = r["reached"] + r["failed"]
            total[0] += r["reached"]
            total[1] += r["failed"]
            print(f"| {r['map']} | {r['nodes']} | {k['walk']} | {k['jump']} | {k['duck']} | {k['swim']} "
                  f"| {k['climb']} | {k['ride']} | {r['reached']} of {goals} "
                  f"({100 * r['reached'] / max(goals, 1):.0f}%) | {r['failed']} "
                  f"| {100 * r['visited']:.0f}% |")
        causes = Counter()
        for job in jobs:
            causes.update(job.result()["causes"])
        print("\ngoals given up, by what the player was doing:")
        for name, count in causes.most_common():
            print(f"  {count:5d}  {name}")
        print(f"\nall maps: {total[0]} of {sum(total)} goals reached "
              f"({100 * total[0] / max(sum(total), 1):.1f}%)")
