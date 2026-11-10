# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""How and where a policy's episodes end, with the last steps of each one
that did not reach the exit.

    python rl/tools/deaths.py <weights.pt | teacher> --maps base3@base2a --episodes 100 --json out.json
    python rl/tools/deaths.py --read out.json [more.json ...]

The episodes are the ones q2rl.evaluate plays with the same settings. For
every episode the file has how it ended, by what, and where; for those that
ended badly, the last TRAIL steps: where the player was, whether the ground
about its feet was marked as harmful, the damage taken, what it did and what
the teacher would have done.
"""

import argparse
import json
import sys
from collections import Counter, deque
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Q2VecEnv, layout as L  # noqa: E402
from q2rl.data import CPUS, to_torch  # noqa: E402
from q2rl.evaluate import EVAL_SEED, load  # noqa: E402

TRAIL = 60
SELF_HURT, SELF_DUCKED = 29, 28

# the game's MOD_ numbers that end a player in these maps
MEANS = {0: "unknown", 1: "blaster", 2: "shotgun", 3: "super shotgun", 4: "machinegun", 5: "chaingun",
         6: "grenade", 7: "grenade splash", 8: "rocket", 9: "rocket splash", 10: "hyperblaster",
         11: "railgun", 17: "drowned", 18: "slime", 19: "lava", 20: "crushed", 22: "fell",
         25: "explosion", 26: "barrel", 29: "splash", 30: "laser", 31: "hurt trigger", 32: "hit",
         33: "blaster trap"}
# rl_monsters in game/g_rl_obs.c, from 1
MONSTERS = ("", "light guard", "guard", "shotgun guard", "infantry", "gunner", "berserker", "gladiator",
            "tank", "tank commander", "medic", "flipper", "chick", "parasite", "flyer", "brain",
            "floater", "hover", "mutant", "supertank", "boss2", "jorg", "makron")


def cause(r):
    if r["done"] != L.DONE_DEATH:
        return L.DONE_NAMES[r["done"]]
    if r["death_by"]:
        return MONSTERS[r["death_by"]]
    return MEANS.get(r["death_means"], f"means {r['death_means']}")


def play(policy, maps, episodes, device, sample, envs=28, time_limit=6000, seed=EVAL_SEED):
    env = Q2VecEnv(envs, maps=maps, time_limit=time_limit, cpus=CPUS, seed=seed, mode=L.MODE_PLAY,
                   guided=policy.guided if policy else True)
    obs = env.reset()
    state = None
    starts = torch.ones(envs, device=device)
    last = torch.tensor(L.ACT_IDLE, device=device).repeat(envs, 1)
    quota = np.full(envs, episodes // envs)
    quota[:episodes % envs] += 1
    played = np.zeros(envs, int)
    trails = [deque(maxlen=TRAIL) for _ in range(envs)]
    out = []
    while (played < quota).any():
        before = [(e.block["origin"].tolist(), obs["self"][i, L.SELF_HARM:L.SELF_HARM + L.FOOT_WAYS].tolist(),
                   float(obs["self"][i, SELF_DUCKED]), env.teacher[i].tolist())
                  for i, e in enumerate(env.engines)]
        if policy is None:
            acted = env.teacher.copy()
            obs, _, done = env.step(None)
        else:
            actions, _, _, state = policy.act(to_torch(obs, device), last, state, starts, sample)
            acted = actions.cpu().numpy()
            obs, _, done = env.step(acted)
            ended = torch.from_numpy(done != L.DONE_NO).to(device)
            starts = ended.float()
            last = torch.where(ended[:, None], torch.tensor(L.ACT_IDLE, device=device), actions)
        for i in range(envs):
            origin, harm, ducked, teacher = before[i]
            hurt = 0.0 if done[i] else float(obs["self"][i, SELF_HURT])
            trails[i].append({"at": [round(x) for x in origin], "harm": harm, "ducked": ducked,
                              "did": acted[i].tolist(), "teacher": teacher, "hurt": hurt})
            if done[i]:
                if played[i] < quota[i]:
                    r = dict(env.info[i])
                    if r["done"] != L.DONE_EXIT:
                        r["trail"] = list(trails[i])
                    out.append(r)
                played[i] += 1
                trails[i].clear()
    env.close()
    return out


def where(r, cell=128):
    return tuple(int(np.floor(x / cell)) * cell for x in r["origin"][:2])


def report(results):
    for m in sorted({r["map"] for r in results}):
        rs = [r for r in results if r["map"] == m]
        causes = Counter(cause(r) for r in rs)
        print(f"{m}: {len(rs)} episodes")
        for name, n in causes.most_common():
            bad = [r for r in rs if cause(r) == name]
            line = f"  {name}: {n}"
            if name != "exit":
                spots = Counter(where(r) for r in bad).most_common(3)
                line += "   at " + ", ".join(f"({x},{y}) x{k}" for (x, y), k in spots)
            if name == "laser":
                warned = sum(any(max(s["harm"]) > 0 for s in r["trail"][-12:-1]) for r in bad)
                line += f"   harm shown in the 12 steps before, in {warned}"
            print(line)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("policy", nargs="?", help="weights, or 'teacher'")
    ap.add_argument("--maps", nargs="+", default=["base1"])
    ap.add_argument("--episodes", type=int, default=100, help="in all, shared among the maps")
    ap.add_argument("--sample", action="store_true")
    ap.add_argument("--json", default="")
    ap.add_argument("--read", nargs="+", help="report on files written before instead of playing")
    args = ap.parse_args()
    if args.read:
        results = [r for f in args.read for r in json.loads(Path(f).read_text())]
    else:
        device = "cuda" if torch.cuda.is_available() else "cpu"
        policy = None if args.policy == "teacher" else load(args.policy, device)
        results = play(policy, args.maps, args.episodes, device, args.sample)
        if args.json:
            Path(args.json).write_text(json.dumps(results))
    report(results)
