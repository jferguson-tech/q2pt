# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""A unit played through as one run: every leg starts with exactly what the
same run carried out of the leg before.

    python rl/tools/unit.py [--unit base] [--runs 100] [--first 0] [--steps 6000] [--demos folder]
    python rl/tools/unit.py --policy ~/q2pt-rl/weights/ppo.pt [--sample | --cool 0.25]

The teacher plays unless a policy is named. A run goes on to the next leg
only when it reaches the exit that leads on; the table says how many runs
got through each leg and how many through the whole unit. This is a harder
thing than the legs measured one at a time, where each episode starts from
what the teacher carried: here a player that leaves a map with little health
has to start the next with it.

Runs take the seeds from --first on. --demos writes the first run's legs as
demos, one file a leg.
"""

import argparse
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402
from q2env.maps import UNITS  # noqa: E402

CPUS = list(range(8, 16)) + list(range(24, 32)) + list(range(2, 8)) + list(range(18, 24))


def run(legs, seed, steps, cpu, policy, sample, demos, temperature=1.0):
    """How many legs the run got through, and how it ended."""
    import torch
    carry = np.zeros(L.CARRY_INTS, np.int32)
    if sample:
        torch.manual_seed(seed)     # the same draws each time the run is played
    with Engine(cpu=cpu) as e:
        b = e.block
        for n, (name, start, onward) in enumerate(legs):
            # as Engine.ask_reset does, but with this run's own carry
            b["map"], b["onward"], b["back"] = start.encode(), onward.encode(), b""
            b["start"] = carry
            b["seed"], b["skill"], b["time_limit"], b["flags"], b["mode"] = seed, 1, steps, 0, L.MODE_PLAY
            b["demo"] = str(demos / f"unit_{name.replace('@', '_from_')}.dm2").encode() if demos else b""
            e.ask(L.REQ_RESET)
            e.wait()
            state = None
            if policy is not None:
                device = next(policy.parameters()).device
                last = torch.tensor([L.ACT_IDLE], device=device)
                first = torch.ones(1, device=device)
            while not b["done"]:
                if policy is None:
                    e.step(None)
                    continue
                from q2rl.data import to_torch
                obs = {k: b[k][None].copy() for k in L.OBS_FIELDS}
                if not policy.guided:
                    obs["guide"][:] = 0
                action, _, _, state = policy.act(to_torch(obs, device), last, state, first, sample=sample,
                                                    temperature=temperature)
                e.step(action[0].cpu().numpy())
                last, first = action, torch.zeros(1, device=device)
            if b["done"] != L.DONE_EXIT:
                return n, L.DONE_NAMES[b["done"]], int(b["self"][0])
            carry = np.array(b["carry"])
        return len(legs), "through", int(carry[L.CARRY_HEALTH])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", default="base")
    ap.add_argument("--runs", type=int, default=100)
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--policy", default="")
    ap.add_argument("--sample", action="store_true")
    ap.add_argument("--cool", type=float, default=0.0, help="draw actions at this temperature")
    ap.add_argument("--demos", default="")
    args = ap.parse_args()

    legs = UNITS[args.unit]
    policy = None
    if args.policy:
        from q2rl.evaluate import load
        policy = load(args.policy, "cpu")
    demos = Path(args.demos).expanduser().resolve() if args.demos else None
    with ThreadPoolExecutor(1 if policy is not None else len(CPUS)) as pool:
        jobs = [pool.submit(run, legs, seed, args.steps, CPUS[seed % len(CPUS)], policy,
                            args.sample or args.cool > 0, demos if seed == args.first else None, args.cool or 1.0)
                for seed in range(args.first, args.first + args.runs)]
        ends = [j.result() for j in jobs]

    print(f"{args.policy or 'teacher'}: {args.runs} runs of the {args.unit} unit\n")
    print("| leg | runs that began it | reached its exit | died | out of time | left by another exit |")
    print("| --- | --- | --- | --- | --- | --- |")
    for n, (name, _, _) in enumerate(legs):
        began = [x for x in ends if x[0] >= n]
        here = [x for x in began if x[0] == n]
        print(f"| {name} | {len(began)} | {len(began) - len(here)} | {sum(x[1] == 'death' for x in here)} "
              f"| {sum(x[1] == 'time' for x in here)} | {sum(x[1] == 'wrong' for x in here)} |")
    through = [x for x in ends if x[0] == len(legs)]
    print(f"\nthrough the whole unit: {len(through)} of {args.runs}"
          + (f", ending with {np.mean([x[2] for x in through]):.0f} health" if through else ""))
