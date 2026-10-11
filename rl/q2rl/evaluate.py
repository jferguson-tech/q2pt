# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""How often a policy, or the teacher, reaches the exit.

    python -m q2rl.evaluate <weights.pt | teacher> [--episodes 100] [--maps base1] [--sample]

Seeds are 500000 and up, which no training run uses. Each environment plays
its own run of seeds, so two things evaluated with the same settings meet
the same episodes. By default the policy plays its most likely choice in
each branch; --sample draws from its distribution instead.
"""

import argparse
import sys
from collections import Counter
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Q2VecEnv, layout as L  # noqa: E402
from q2rl.data import CPUS, to_torch  # noqa: E402
from q2rl.model import Policy  # noqa: E402

EVAL_SEED = 500000


def load(path, device):
    saved = torch.load(path, map_location=device)
    policy = Policy(hidden=saved["hidden"], guided=saved["guided"]).to(device)
    policy.load_state_dict(saved["model"])
    policy.eval()
    return policy


def evaluate(policy, episodes=100, maps=("base1",), device="cuda", sample=False, envs=28,
             time_limit=6000, skill=1, seed=EVAL_SEED):
    """policy: a Policy, or None for the teacher. Returns the list of what
    the game reported at the end of each episode."""
    env = Q2VecEnv(envs, maps=maps, skill=skill, time_limit=time_limit, cpus=CPUS, seed=seed,
                   mode=L.MODE_PLAY, guided=policy.guided if policy else True)
    obs = env.reset()
    results = []
    state = None
    starts = torch.ones(envs, device=device)
    last = torch.tensor(L.ACT_IDLE, device=device).repeat(envs, 1)
    # every environment plays the same number of episodes, so that the set of
    # episodes does not depend on which finish first
    quota = np.full(envs, episodes // envs)
    quota[:episodes % envs] += 1
    played = np.zeros(envs, int)
    while (played < quota).any():
        if policy is None:
            obs, _, done = env.step(None)
        else:
            actions, _, _, state = policy.act(to_torch(obs, device), last, state, starts, sample)
            obs, _, done = env.step(actions.cpu().numpy())
            ended = torch.from_numpy(done != L.DONE_NO).to(device)
            starts = ended.float()
            last = torch.where(ended[:, None], torch.tensor(L.ACT_IDLE, device=device), actions)
        for i in np.nonzero(done)[0]:
            if played[i] < quota[i]:
                results.append(env.info[i])
            played[i] += 1
    env.close()
    return results


def by_map(results):
    """The summary of each map's episodes, by map."""
    maps = sorted({r["map"] for r in results})
    return {m: summary([r for r in results if r["map"] == m]) for m in maps}


def summary(results):
    won = sum(r["done"] == L.DONE_EXIT for r in results)
    ends = Counter(L.DONE_NAMES[r["done"]] for r in results)
    return {"episodes": len(results), "exit": won, "rate": won / max(len(results), 1),
            "death": ends["death"], "time": ends["time"],
            "kills": float(np.mean([r["kills"] for r in results])),
            "steps": float(np.mean([r["steps"] for r in results]))}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("policy", help="weights, or 'teacher'")
    ap.add_argument("--episodes", type=int, default=100, help="in all, shared among the maps")
    ap.add_argument("--maps", nargs="+", default=["base1"])
    ap.add_argument("--sample", action="store_true", help="draw actions; otherwise the likeliest is taken")
    ap.add_argument("--seed", type=int, default=EVAL_SEED)
    ap.add_argument("--json", default="", help="a file to write the figures to")
    args = ap.parse_args()
    device = "cuda" if torch.cuda.is_available() else "cpu"
    policy = None if args.policy == "teacher" else load(args.policy, device)
    results = evaluate(policy, args.episodes, args.maps, device, args.sample, seed=args.seed)
    s = summary(results)
    how = "teacher" if policy is None else "sampled" if args.sample else "greedy"
    print(f"{args.policy} ({how}): {s['exit']} of {s['episodes']} reached the exit ({100 * s['rate']:.0f}%), "
          f"{s['death']} died, {s['time']} ran out of time; {s['kills']:.1f} kills and "
          f"{s['steps'] / 10:.0f} s an episode")
    maps = by_map(results)
    for m, x in maps.items():
        print(f"  {m}: {x['exit']} of {x['episodes']} ({100 * x['rate']:.0f}%), {x['death']} died, "
              f"{x['time']} out of time")
    if args.json:
        import json
        from pathlib import Path
        Path(args.json).write_text(json.dumps({"policy": str(args.policy), "how": how, "seed": args.seed,
                                               "all": s, "maps": maps}, indent=1))
