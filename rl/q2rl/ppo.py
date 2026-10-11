# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""PPO, from imitation weights or from nothing.

    python -m q2rl.ppo --name ppo_ft --start dagger [--steps 3000000]
    python -m q2rl.ppo --name ppo_scratch [--steps 3000000]

28 environments are stepped together for 128 steps; the policy is then
updated on those 3,584 steps and the next 128 are played. Advantages are
estimated with GAE. The network is recurrent: each environment's 128 steps
are one sequence, unrolled from the state the network had when they began.

The reward of a step is made here from the parts the game reports:

    +0.05   for each second of the planner's route gained (a step can gain
            or lose at most 0.2 s: the planner's change of mind is not paid)
    +0.005  for each point of damage done to a monster
    +0.5    for a kill
    +10     for reaching the exit
    -0.01   for each point of damage taken
    -5      for dying
    -0.002  for every step

Started from imitation weights (--start), two things are added:

    for the first --value-only steps only the value head is trained, the
    rest of the network being left as it was, so that the first policy
    updates are not made on the advantages of an untrained critic;

    a penalty for straying from the teacher, the cross entropy between the
    policy and the teacher's action in every state, with a weight that
    falls in a straight line from --teacher-coef over the first
    --teacher-steps steps, down to --teacher-floor, where it stays.

From nothing there is neither: the same reward and the same number of steps.
Every --eval-every steps the policy plays 56 evaluation episodes on seeds
the training never sees; training waits while it does.
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
from q2env import Q2VecEnv, layout as L  # noqa: E402
from q2env.engine import data_dir  # noqa: E402
from q2rl.bc import save  # noqa: E402
from q2rl.data import CPUS, to_torch  # noqa: E402
from q2rl.evaluate import evaluate, load, summary  # noqa: E402
from q2rl.model import Policy  # noqa: E402

ENVS, HORIZON = 28, 128
GAMMA, LAMBDA = 0.995, 0.95
CLIP, EPOCHS, MINIBATCHES = 0.2, 3, 4
VALID_SEED = 400000     # training plays from 7000000 up, the final evaluation from 500000
ENTROPY, VALUE_COEF = 0.003, 0.5


def reward(gain, done):
    r = (0.05 * np.clip(gain[:, L.GAIN_PROGRESS], -0.2, 0.2)
         + 0.005 * gain[:, L.GAIN_DEALT] + 0.5 * gain[:, L.GAIN_KILLS]
         - 0.01 * gain[:, L.GAIN_TAKEN] - 0.002)
    r = r + 10.0 * (done == L.DONE_EXIT) - 5.0 * (done == L.DONE_DEATH)
    return r.astype(np.float32)


def log_prob_entropy(logits, actions):
    logp, entropy = 0, 0
    for i, lg in enumerate(logits):
        dist = torch.distributions.Categorical(logits=lg)
        logp = logp + dist.log_prob(actions[..., i])
        entropy = entropy + dist.entropy()
    return logp, entropy


if __name__ == "__main__":
    from torch.utils.tensorboard import SummaryWriter

    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--start", default="")
    ap.add_argument("--maps", nargs="+", default=["base1"])
    ap.add_argument("--steps", type=int, default=3_000_000)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--value-only", type=int, default=200_000)
    ap.add_argument("--teacher-coef", type=float, default=0.5)
    ap.add_argument("--teacher-steps", type=int, default=1_500_000)
    ap.add_argument("--teacher-floor", type=float, default=0.1,
                    help="what the teacher's penalty comes down to and stays at; 0 to let it go")
    ap.add_argument("--eval-every", type=int, default=500_000)
    ap.add_argument("--unguided", action="store_true")
    ap.add_argument("--seed", type=int, default=1, help="of the actions drawn, the batches and the episodes played")
    args = ap.parse_args()

    device = "cuda"
    torch.manual_seed(args.seed)
    weights = data_dir() / "weights"
    if args.start:
        policy = load(weights / f"{args.start}.pt", device)
    else:
        policy = Policy(guided=not args.unguided).to(device)
    value_only = args.value_only if args.start else 0
    teacher_steps = args.teacher_steps if args.start else 0
    opt = torch.optim.Adam(policy.parameters(), lr=args.lr)
    writer = SummaryWriter(data_dir() / "runs" / args.name)

    env = Q2VecEnv(ENVS, maps=args.maps, time_limit=4500, cpus=CPUS, seed=7_000_000 + 100_000 * args.seed,
                   mode=L.MODE_PLAY, guided=policy.guided)
    obs = env.reset()
    idle = torch.tensor(L.ACT_IDLE, device=device)
    last = idle.repeat(ENVS, 1)
    starts = torch.ones(ENVS, device=device)
    state = torch.zeros(1, ENVS, policy.hidden, device=device)

    curve = []                      # what is plotted: steps, share of training episodes to the exit, evaluations
    recent = []                     # how the last training episodes ended
    total, next_eval, update = 0, 0, 0
    began = time.time()

    best = [-1.0]           # the best validation rate so far

    def checkpoint():
        # Played with the likeliest action on seeds that neither training nor
        # the final evaluation uses. The weights kept under the run's name
        # are those that did best here; the last are kept beside them.
        s = summary(evaluate(policy, 28 * len(args.maps), args.maps, device, sample=False, seed=VALID_SEED))
        won = [e == L.DONE_EXIT for e in recent[-200:]]
        entry = {"steps": total, "train_exit_rate": float(np.mean(won)) if won else 0.0,
                 "train_episodes": len(recent), "eval": s, "seconds": time.time() - began}
        curve.append(entry)
        writer.add_scalar("ppo/eval_exit_rate", s["rate"], total)
        print(f"{total:>9} steps: training episodes to the exit {100 * entry['train_exit_rate']:.0f}% "
              f"(last {len(won)}); evaluated {s['exit']} of {s['episodes']} to the exit, {s['death']} died, "
              f"{s['time']} out of time; {entry['seconds'] / 60:.0f} min", flush=True)
        entry["kept"] = s["rate"] > best[0]
        (weights / f"{args.name}.json").write_text(json.dumps(curve, indent=1))
        save(policy, weights / f"{args.name}_last.pt", maps=args.maps, steps=total)
        if s["rate"] > best[0]:
            best[0] = s["rate"]
            save(policy, weights / f"{args.name}.pt", maps=args.maps, steps=total)

    while total < args.steps:
        if total >= next_eval:
            checkpoint()
            next_eval += args.eval_every

        # ---- play
        policy.eval()
        buf = {k: [] for k in (*L.OBS_FIELDS, "last", "action", "teacher", "logp", "value", "reward", "start", "end")}
        state0 = state.clone()
        for t in range(HORIZON):
            tobs = to_torch(obs, device)
            teacher = torch.from_numpy(env.teacher).to(device).long()
            action, logp, value, state = policy.act(tobs, last, state, starts)
            for k in L.OBS_FIELDS:
                buf[k].append(tobs[k])
            buf["last"].append(last)
            buf["action"].append(action)
            buf["teacher"].append(teacher)
            buf["logp"].append(logp)
            buf["value"].append(value)
            buf["start"].append(starts)
            obs, gain, done = env.step(action.cpu().numpy())
            ended = done != L.DONE_NO
            buf["reward"].append(torch.from_numpy(reward(gain, done)).to(device))
            buf["end"].append(torch.from_numpy(ended).to(device).float())
            recent.extend(int(d) for d in done[ended])
            starts = torch.from_numpy(ended).to(device).float()
            last = torch.where(starts[:, None].bool(), idle, action)
        total += ENVS * HORIZON
        with torch.no_grad():
            _, _, next_value, _ = policy.act(to_torch(obs, device), last, state, starts)
        b = {k: torch.stack(v, 1) for k, v in buf.items()}       # (envs, horizon, ...)

        # ---- advantages. An episode that ran out of time is cut off, not
        # ended: nothing is bootstrapped past it either, which undervalues
        # its last states a little.
        adv = torch.zeros(ENVS, HORIZON, device=device)
        running = torch.zeros(ENVS, device=device)
        for t in reversed(range(HORIZON)):
            nv = next_value if t == HORIZON - 1 else b["value"][:, t + 1]
            alive = 1 - b["end"][:, t]
            delta = b["reward"][:, t] + GAMMA * nv * alive - b["value"][:, t]
            running = delta + GAMMA * LAMBDA * alive * running
            adv[:, t] = running
        returns = adv + b["value"]

        # ---- learn
        policy.train()
        frozen = total <= value_only
        coef = max(args.teacher_floor, args.teacher_coef * (1 - total / teacher_steps)) if teacher_steps else 0.0
        stats = []
        for epoch in range(EPOCHS):
            order = torch.randperm(ENVS, device=device)
            for part in order.chunk(MINIBATCHES):
                o = {k: b[k][part] for k in L.OBS_FIELDS}
                logits, value, _ = policy(o, b["last"][part], state0[:, part], b["start"][part])
                value_loss = F.mse_loss(value, returns[part])
                if frozen:
                    loss = VALUE_COEF * value_loss
                    pg = ent = stray = torch.zeros((), device=device)
                else:
                    logp, entropy = log_prob_entropy(logits, b["action"][part])
                    a = adv[part]
                    a = (a - a.mean()) / (a.std() + 1e-8)
                    ratio = (logp - b["logp"][part]).exp()
                    pg = -torch.min(ratio * a, ratio.clamp(1 - CLIP, 1 + CLIP) * a).mean()
                    ent = entropy.mean()
                    stray = sum(F.cross_entropy(lg.flatten(0, 1), b["teacher"][part][..., i].flatten())
                                for i, lg in enumerate(logits))
                    loss = pg + VALUE_COEF * value_loss - ENTROPY * ent + coef * stray
                opt.zero_grad(set_to_none=True)
                loss.backward()
                if frozen:
                    # only the value head moves
                    for name, p in policy.named_parameters():
                        if not name.startswith("value.") and p.grad is not None:
                            p.grad.zero_()
                torch.nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
                opt.step()
                stats.append((value_loss.item(), pg.item(), ent.item(), stray.item()))
        update += 1
        if update % 10 == 0:
            v, p, e, s = np.mean(stats, 0)
            writer.add_scalar("ppo/value_loss", v, total)
            writer.add_scalar("ppo/entropy", e, total)
            writer.add_scalar("ppo/stray_from_teacher", s, total)
            writer.add_scalar("ppo/reward_per_step", b["reward"].mean().item(), total)
            if recent:
                writer.add_scalar("ppo/train_exit_rate", np.mean([x == L.DONE_EXIT for x in recent[-200:]]), total)

    checkpoint()
    env.close()
