# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Behaviour cloning: the policy is trained to give the teacher's action in
the states of the teacher's own play.

    python -m q2rl.bc --name bc [--episodes 600] [--epochs 12] [--unguided]

Records the teacher playing (once: the recording is kept and used again),
trains, saves ~/q2pt-rl/weights/<name>.pt and evaluates it.

The loss is the sum over the action's seven branches of the cross entropy
between the policy's choice and the teacher's. The network is unrolled over
windows of 64 steps with its state cleared where an episode starts inside
one; the state is not carried from one window to the next, so the first
steps of each window are learned with less memory than play has.
"""

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import layout as L  # noqa: E402
from q2env.engine import data_dir  # noqa: E402
from q2rl.data import Dataset, collect, to_torch  # noqa: E402
from q2rl.evaluate import evaluate, summary  # noqa: E402
from q2rl.model import Policy  # noqa: E402

TRAIN_SEED = 0          # evaluation uses 500000 and up


def imitation_loss(policy, batch):
    """The cloning loss on one batch of windows, and the share of steps on
    which each branch's most likely choice is the teacher's."""
    logits, _, _ = policy(to_torch(batch, batch["start"].device), batch["last"], None, batch["start"])
    teacher = batch["teacher"].long()
    loss, right = 0, []
    for i, lg in enumerate(logits):
        loss = loss + F.cross_entropy(lg.flatten(0, 1), teacher[..., i].flatten())
        right.append((lg.argmax(-1) == teacher[..., i]).float().mean())
    return loss, torch.stack(right)


def train(policy, dataset, epochs, device, lr=3e-4, batch=64, writer=None, tag="bc", step0=0):
    """Trains on every window of the dataset `epochs` times. Returns the
    number of batches done, counted on from step0, and the last epoch's
    mean loss and per-branch accuracy."""
    opt = torch.optim.Adam(policy.parameters(), lr=lr)
    rng = np.random.default_rng(step0)
    step = step0
    policy.train()
    for epoch in range(epochs):
        losses, rights = [], []
        for b in dataset.batches(batch, device, rng):
            loss, right = imitation_loss(policy, b)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(policy.parameters(), 1.0)
            opt.step()
            losses.append(loss.item())
            rights.append(right.detach())
            step += 1
            if writer and step % 50 == 0:
                writer.add_scalar(f"{tag}/loss", losses[-1], step)
        right = torch.stack(rights).mean(0).cpu().numpy()
        if writer:
            writer.add_scalar(f"{tag}/epoch_loss", np.mean(losses), step)
            writer.add_scalar(f"{tag}/accuracy", right.mean(), step)
    policy.eval()
    return step, float(np.mean(losses)), right


def save(policy, path, **more):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"model": policy.state_dict(), "hidden": policy.hidden, "guided": policy.guided, **more}, path)


def teacher_data(maps, episodes, name=None, seed=0):
    """The folder holding the teacher's own play on these maps, recorded now
    if it was not before. seed picks which episodes: a run of its own for
    each training seed."""
    out = data_dir() / "data" / (name or f"teacher_{'_'.join(maps)}_{episodes}" + (f"_s{seed}" if seed else ""))
    if not (out / "episodes.json").exists():
        start = time.time()
        results = collect(lambda obs, starts: (None, None), episodes, maps, out, TRAIN_SEED + 50_000 * seed,
                          time_limit=4500)
        won = sum(r["done"] == L.DONE_EXIT for r in results)
        print(f"recorded {len(results)} teacher episodes, {sum(r['steps'] for r in results)} steps, "
              f"{won} to the exit, in {time.time() - start:.0f} s", flush=True)
    return out


if __name__ == "__main__":
    from torch.utils.tensorboard import SummaryWriter

    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="bc")
    ap.add_argument("--maps", nargs="+", default=["base1"])
    ap.add_argument("--episodes", type=int, default=600)
    ap.add_argument("--epochs", type=int, default=12)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--unguided", action="store_true")
    ap.add_argument("--eval", type=int, default=100)
    ap.add_argument("--seed", type=int, default=0, help="of the weights' start, the batches and the teacher's episodes")
    args = ap.parse_args()

    device = "cuda"
    torch.manual_seed(args.seed)
    folder = teacher_data(args.maps, args.episodes, seed=args.seed)
    data = Dataset([folder])
    print(f"{data.steps} steps in {data.windows} windows", flush=True)

    policy = Policy(hidden=args.hidden, guided=not args.unguided).to(device)
    writer = SummaryWriter(data_dir() / "runs" / args.name)
    start = time.time()
    step, loss, right = train(policy, data, args.epochs, device, writer=writer)
    print(f"trained {step} batches in {time.time() - start:.0f} s: loss {loss:.3f}, "
          f"agrees with the teacher on {' '.join(f'{100 * r:.0f}%' for r in right)} of steps "
          f"(forward, strafe, jump, yaw, pitch, fire, weapon)", flush=True)
    path = data_dir() / "weights" / f"{args.name}.pt"
    save(policy, path, maps=args.maps, steps=data.steps)

    result = {"name": args.name, "steps": data.steps, "loss": loss, "accuracy": [float(r) for r in right]}
    for sample in (False, True):
        s = summary(evaluate(policy, args.eval, args.maps, device, sample))
        result["sampled" if sample else "greedy"] = s
        print(f"{'sampling' if sample else 'most likely action'}: {s['exit']} of {s['episodes']} reached "
              f"the exit, {s['death']} died, {s['time']} out of time", flush=True)
    (data_dir() / "weights" / f"{args.name}.json").write_text(json.dumps(result, indent=1))
