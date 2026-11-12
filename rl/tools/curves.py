# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Draws the learning curves of the PPO runs on one pair of axes.

    python rl/tools/curves.py [--out ~/q2pt-rl/plots/curves.png] run[=label] ...
        [--line label=percent ...]

Each run is a name under ~/q2pt-rl/weights/ whose .json holds what ppo.py
wrote at each evaluation. Solid lines are the evaluations (56 episodes on
seeds the training never sees, most likely action); faint lines are the
share of the run's own last 200 training episodes that reached the exit.
--line draws a level for something that is not trained by PPO, such as the
teacher or a cloned student.
"""

import argparse
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env.engine import data_dir  # noqa: E402

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--line", action="append", default=[])
    ap.add_argument("--out", default=str(data_dir() / "plots" / "curves.png"))
    args = ap.parse_args()

    fig, ax = plt.subplots(figsize=(9, 5.5))
    for i, run in enumerate(args.runs):
        name, _, label = run.partition("=")
        curve = json.loads((data_dir() / "weights" / f"{name}.json").read_text())
        steps = [c["steps"] / 1e6 for c in curve]
        colour = f"C{i}"
        ax.plot(steps, [100 * c["eval"]["rate"] for c in curve], "o-", color=colour, label=label or name)
        ax.plot(steps[1:], [100 * c["train_exit_rate"] for c in curve[1:]], "-", color=colour, alpha=0.3)
    for i, line in enumerate(args.line):
        label, _, value = line.partition("=")
        ax.axhline(float(value), linestyle="--", color=f"C{len(args.runs) + i}", label=label)
    ax.set_xlabel("environment steps, millions (one step is 100 ms of the game)")
    ax.set_ylabel("episodes that reach the exit, %")
    ax.set_ylim(0, 100)
    ax.grid(alpha=0.3)
    ax.legend(loc="lower right")
    ax.set_title("base1, skill 1: solid, 56 evaluation episodes; faint, last 200 training episodes")
    out = Path(args.out).expanduser()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=110, bbox_inches="tight")
    print(out)
