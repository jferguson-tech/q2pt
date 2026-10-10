# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""One whole run for one training seed: clone the teacher, DAgger, PPO from
the DAgger student, and an evaluation of each on the maps trained on and on
the maps held out, taking the likeliest action and drawing actions.

    python rl/tools/run_seed.py --seed 0 --tag m3 --maps base3 mintro fact3 --held base1

Weights go to ~/q2pt-rl/weights/<tag>_{bc,dagger,ppo}_s<seed>.pt and the
figures to ~/q2pt-rl/results/<tag>/. Each step is skipped if its output is
there already, so a run that was stopped can be started again. Training and
evaluation take the one graphics card in turn, never together.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env.engine import data_dir  # noqa: E402

RL = Path(__file__).resolve().parents[1]


def run(*words):
    print("+", *words, flush=True)
    subprocess.run([sys.executable, "-m", *map(str, words)], cwd=RL, check=True)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--maps", nargs="+", required=True)
    ap.add_argument("--held", nargs="*", default=[])
    ap.add_argument("--teacher-episodes", type=int, default=900)
    ap.add_argument("--rounds", type=int, default=8)
    ap.add_argument("--round-episodes", type=int, default=300)
    ap.add_argument("--ppo-steps", type=int, default=3_000_000)
    ap.add_argument("--eval", type=int, default=100, help="evaluation episodes per map")
    args = ap.parse_args()

    weights = data_dir() / "weights"
    results = data_dir() / "results" / args.tag
    results.mkdir(parents=True, exist_ok=True)
    s = args.seed
    bc, dagger, ppo = (f"{args.tag}_{k}_s{s}" for k in ("bc", "dagger", "ppo"))

    if not (weights / f"{bc}.pt").exists():
        run("q2rl.bc", "--name", bc, "--maps", *args.maps, "--episodes", args.teacher_episodes,
            "--seed", s, "--eval", 28)
    if not (weights / f"{dagger}.pt").exists():
        run("q2rl.dagger", "--start", bc, "--name", dagger, "--maps", *args.maps,
            "--teacher-episodes", args.teacher_episodes, "--rounds", args.rounds,
            "--episodes", args.round_episodes, "--seed", s, "--eval", 56)
    if not (weights / f"{ppo}.json").exists() or not (weights / f"{ppo}.pt").exists():
        run("q2rl.ppo", "--start", dagger, "--name", ppo, "--maps", *args.maps, "--steps", args.ppo_steps,
            "--seed", s + 1)

    sets = [("train", args.maps)] + ([("held", args.held)] if args.held else [])
    for where, maps in sets:
        episodes = args.eval * len(maps)
        out = results / f"teacher_{where}.json"
        if not out.exists():
            run("q2rl.evaluate", "teacher", "--maps", *maps, "--episodes", episodes, "--json", out)
        for name in (bc, dagger, ppo):
            for how in ("greedy", "sampled"):
                out = results / f"{name}_{where}_{how}.json"
                if out.exists():
                    continue
                run("q2rl.evaluate", weights / f"{name}.pt", "--maps", *maps, "--episodes", episodes,
                    "--json", out, *(["--sample"] if how == "sampled" else []))
