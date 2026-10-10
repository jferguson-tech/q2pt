# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""DAgger: the student drives, the teacher labels, the data is added to and
the student trained again.

    python -m q2rl.dagger --start bc --name dagger [--rounds 8] [--episodes 200]

Starts from cloned weights and the teacher's recording they were trained
on. In each round the student plays, except that at each step the teacher's
action is played instead with a probability that falls from round to round
(0.5, 0.3, 0.1, then 0). Every state met is stored with the teacher's action
for it, whoever acted. The student is then trained on all the data so far
and evaluated. The game gives the teacher's action after every step, so no
state is ever labelled after the fact.
"""

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import layout as L  # noqa: E402
from q2env.engine import data_dir  # noqa: E402
from q2rl.bc import save, teacher_data, train  # noqa: E402
from q2rl.data import Dataset, collect, size_gb, to_torch  # noqa: E402
from q2rl.evaluate import evaluate, load, summary  # noqa: E402

BETAS = (0.5, 0.3, 0.1)     # the teacher's share of the steps, by round; 0 after


class Driver:
    """The student at the controls of a batch of environments, with the
    teacher taking a share of the steps."""

    def __init__(self, policy, envs, beta, device, seed):
        self.policy, self.beta, self.device = policy, beta, device
        self.rng = np.random.default_rng(seed)
        self.state = None
        self.idle = torch.tensor(L.ACT_IDLE, device=device)
        self.last = self.idle.repeat(envs, 1)
        self.env = None

    def __call__(self, obs, starts):
        started = torch.from_numpy(starts).to(self.device)
        self.last = torch.where(started[:, None], self.idle, self.last)
        actions, _, _, self.state = self.policy.act(to_torch(obs, self.device), self.last, self.state,
                                                    started.float(), sample=True)
        actions = actions.cpu().numpy()
        use_teacher = self.rng.random(len(starts)) < self.beta
        return actions, use_teacher

    def took(self, taken):
        self.last = torch.from_numpy(taken).to(self.device)


if __name__ == "__main__":
    from torch.utils.tensorboard import SummaryWriter

    ap = argparse.ArgumentParser()
    ap.add_argument("--start", default="bc")
    ap.add_argument("--name", default="dagger")
    ap.add_argument("--maps", nargs="+", default=["base1"])
    ap.add_argument("--teacher-episodes", type=int, default=600)
    ap.add_argument("--rounds", type=int, default=8)
    ap.add_argument("--episodes", type=int, default=200)
    ap.add_argument("--epochs", type=int, default=6)
    ap.add_argument("--eval", type=int, default=100)
    ap.add_argument("--seed", type=int, default=0, help="of the batches, the mixing and the episodes played")
    args = ap.parse_args()

    device = "cuda"
    torch.manual_seed(args.seed)
    weights = data_dir() / "weights"
    policy = load(weights / f"{args.start}.pt", device)
    folders = [teacher_data(args.maps, args.teacher_episodes, seed=args.seed)]
    writer = SummaryWriter(data_dir() / "runs" / args.name)
    log, step = [], 0

    for r in range(args.rounds):
        beta = BETAS[r] if r < len(BETAS) else 0.0
        start = time.time()
        out = data_dir() / "data" / f"{args.name}_round{r}"
        driver = Driver(policy, 28, beta, device, seed=r + 100 * args.seed)

        # the policy is given the action that was played as its last action,
        # the teacher's where the teacher took the step: collect tells the driver
        results = collect(driver, args.episodes, args.maps, out, seed=1000 * (r + 1) + 100_000 * args.seed,
                          time_limit=4500, on_taken=driver.took)
        folders.append(out)
        played = time.time() - start

        data = Dataset(folders)
        step, loss, right = train(policy, data, args.epochs, device, writer=writer, tag="dagger", step0=step)
        trained = time.time() - start - played
        save(policy, weights / f"{args.name}_round{r}.pt", maps=args.maps, steps=data.steps)

        s = summary(evaluate(policy, args.eval, args.maps, device, sample=False))
        own = sum(x["done"] == L.DONE_EXIT for x in results)
        entry = {"round": r, "beta": beta, "data_steps": data.steps, "loss": loss,
                 "accuracy": float(right.mean()), "collect_exit": own, "collect_episodes": len(results),
                 **{f"eval_{k}": v for k, v in s.items()}}
        log.append(entry)
        writer.add_scalar("dagger/eval_exit_rate", s["rate"], r)
        print(f"round {r}: teacher's share {beta:.1f}; while collecting {own} of {len(results)} reached the "
              f"exit; {data.steps} steps of data; loss {loss:.3f}; evaluated {s['exit']} of {s['episodes']} "
              f"to the exit, {s['death']} died, {s['time']} out of time "
              f"({played:.0f} s playing, {trained:.0f} s training)", flush=True)
        (weights / f"{args.name}.json").write_text(json.dumps(log, indent=1))

    save(policy, weights / f"{args.name}.pt", maps=args.maps, steps=data.steps)
    print(f"data on disk: {size_gb():.1f} GB")
