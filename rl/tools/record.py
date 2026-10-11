# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Records one episode as a demo the game can play.

    python rl/tools/record.py <weights.pt | teacher> <out.dm2> [--map base1] [--seed 0]
        [--greedy] [--tries 1]

The policy samples its actions unless --greedy is given; with --tries the
episode is played up to that many times, on the seed and the ones after it,
until one reaches the exit, and that one is kept. Play the file with
`demomap <name>.dm2` from baseq2/demos, or render it with pt_render.
"""

import argparse
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402
from q2rl.data import to_torch  # noqa: E402
from q2rl.evaluate import load  # noqa: E402

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("policy")
    ap.add_argument("out")
    ap.add_argument("--map", default="base1")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--greedy", action="store_true")
    ap.add_argument("--tries", type=int, default=1)
    args = ap.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    policy = None if args.policy == "teacher" else load(args.policy, device)
    torch.manual_seed(args.seed)
    out = Path(args.out).expanduser().resolve()
    with Engine() as e:
        b = e.block
        for seed in range(args.seed, args.seed + args.tries):
            e.reset(args.map, seed, time_limit=args.steps, mode=L.MODE_PLAY, demo=out)
            state = None
            last = torch.tensor([L.ACT_IDLE], device=device)
            start = torch.ones(1, device=device)
            while not b["done"]:
                if policy is None:
                    e.step(None)
                    continue
                obs = {k: b[k][None].copy() for k in L.OBS_FIELDS}
                if not policy.guided:
                    obs["guide"][:] = 0
                action, _, _, state = policy.act(to_torch(obs, device), last, state, start,
                                                 sample=not args.greedy)
                e.step(action[0].cpu().numpy())
                last, start = action, torch.zeros(1, device=device)
            print(f"seed {seed}: {L.DONE_NAMES[b['done']]} after {b['step'] / 10:.1f} s, "
                  f"{b['monsters_killed']} of {b['monsters_total']} monsters, health {b['self'][0]:.0f}, "
                  f"{b['demo_frames']} frames, {b['demo_dropped']} dropped")
            if b["done"] == L.DONE_EXIT:
                break
    print(out)
