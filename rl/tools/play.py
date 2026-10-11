# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""How often the teacher finishes a map, and how it fails when it does not.

    python rl/tools/play.py [--episodes 100] [--steps 6000] [--skill 1] [--table] [map ...]

Plays seeded episodes with the teacher driving in its playing mode: seeds 0
to episodes-1, the monsters in and shooting. An episode succeeds when the
map's exit is reached. Each failure is put in a class: killed, and by what;
or out of time, and what the teacher was doing in the last steps.

A map can have several exits. The one that counts is one that leads on: the
teacher is told not to take an exit to any map that comes before this one in
the game's order. With no maps named, every single-player map is played;
--table prints one line per map.
"""

import argparse
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402
from q2env.maps import ORDER, back_of  # noqa: E402
from q2env.nav import LINK_NAMES  # noqa: E402

CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))

MONSTERS = ("something", "light guard", "shotgun guard", "machinegun guard", "enforcer",
            "gunner", "berserker", "gladiator", "tank", "tank commander", "medic", "barracuda",
            "iron maiden", "parasite", "flyer", "brain", "technician", "icarus", "mutant",
            "supertank", "hornet", "jorg", "makron")
MEANS = {17: "drowned", 18: "slime", 19: "lava", 20: "crushed", 22: "fell", 25: "a wall blown up",
         26: "a barrel", 30: "a laser", 31: "a trigger that hurts", 33: "a blaster trap", 24: "its own rocket",
         16: "its own grenade", 9: "its own rocket", 7: "its own grenade"}
JOBS = ("the way", "the exit", "a thing to touch", "a thing to shoot", "an item", "a monster to kill")


def how_it_ended(block, trail):
    done = int(block["done"])
    if done == L.DONE_EXIT:
        return "exit"
    if done == L.DONE_DEATH:
        by, means = int(block["death_by"]), int(block["death_means"])
        if by:
            return f"killed by a {MONSTERS[by] if by < len(MONSTERS) else 'monster'}"
        return f"killed: {MEANS.get(means, f'means {means}')}"
    # out of time: what was it doing?
    last = trail[-100:]
    span = np.ptp(np.stack([t[0] for t in last]), axis=0).max()
    how = "standing still" if span < 8 else "going to and fro" if span < 150 else "on the move"
    job, link, left, fighting = last[-1][1:]
    if fighting:
        what = "fighting"
    elif left < 0:
        what = "with no plan"
    else:
        what = f"making for {JOBS[job]}" + (f" ({LINK_NAMES[link]})" if link >= 0 else "")
    return f"out of time, {how}, {what}"


def play(map, seed, steps, skill, back, cpu):
    with Engine(cpu=cpu) as e:
        e.reset(map, seed, skill=skill, time_limit=steps, mode=L.MODE_PLAY, back=back)
        b = e.block
        trail = []
        while not b["done"]:
            e.step(None)
            trail.append((b["origin"].copy(), int(b["job_kind"]), int(b["link_type"]),
                          float(b["route_left"]), int(b["fighting"])))
        return {"end": how_it_ended(b, trail), "steps": int(b["step"]),
                "kills": int(b["monsters_killed"]), "monsters": int(b["monsters_total"]),
                "health": float(b["self"][0]), "seed": seed}


def measure(map, episodes, steps, skill):
    back = back_of(map)
    with ThreadPoolExecutor(len(CPUS)) as pool:
        jobs = [pool.submit(play, map, seed, steps, skill, back, CPUS[seed % len(CPUS)])
                for seed in range(episodes)]
        return [j.result() for j in jobs]


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--episodes", type=int, default=100)
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--skill", type=int, default=1)
    ap.add_argument("--table", action="store_true")
    args = ap.parse_args()

    if args.table:
        print("| map | exit reached | killed | out of time | the commonest failure |")
        print("| --- | --- | --- | --- | --- |")
    for map in args.maps or ORDER:
        runs = measure(map, args.episodes, args.steps, args.skill)
        won = [r for r in runs if r["end"] == "exit"]
        ends = Counter(r["end"] for r in runs if r["end"] != "exit")
        if args.table:
            killed = sum(c for e, c in ends.items() if e.startswith("killed"))
            worst = ends.most_common(1)[0] if ends else ("", 0)
            print(f"| {map} | {len(won)} of {len(runs)} | {killed} | {len(runs) - len(won) - killed} "
                  f"| {worst[0]}{f' ({worst[1]})' if worst[1] else ''} |", flush=True)
            continue
        print(f"{map}: {len(won)} of {len(runs)} episodes reached the exit "
              f"({100 * len(won) / len(runs):.0f}%), skill {args.skill}, {args.steps} steps allowed")
        if won:
            print(f"  those took {np.median([r['steps'] for r in won]) / 10:.0f} s (median), killed "
                  f"{np.mean([r['kills'] for r in won]):.1f} of {won[0]['monsters']} monsters, "
                  f"ended with {np.mean([r['health'] for r in won]):.0f} health")
        for end, count in ends.most_common():
            seeds = [r["seed"] for r in runs if r["end"] == end][:6]
            print(f"  {count:4d}  {end}   (seeds {', '.join(str(x) for x in seeds)})")
