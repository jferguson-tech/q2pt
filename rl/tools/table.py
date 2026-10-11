# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The figures of run_seed.py's runs as a table: for each player and each
map, the share of episodes that reached the exit, as the mean over the
training seeds and the spread between them (the standard deviation of the
seeds' own rates, n-1), taking the likeliest action and, beside it, drawing
actions.

    python rl/tools/table.py --tag m3
"""

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env.engine import data_dir  # noqa: E402


def cell(rates):
    if not rates:
        return ""
    r = 100 * np.array(rates)
    if len(r) == 1:
        return f"{r[0]:.0f}%"
    return f"{r.mean():.0f}% ± {r.std(ddof=1):.0f}"


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    args = ap.parse_args()
    folder = data_dir() / "results" / args.tag

    for where in ("train", "held"):
        teacher = folder / f"teacher_{where}.json"
        if not teacher.exists():
            continue
        t = json.loads(teacher.read_text())
        maps = sorted(t["maps"])
        print(f"\n{'maps trained on' if where == 'train' else 'maps held out'}: "
              f"{t['all']['episodes'] // len(maps)} episodes a map, a seed\n")
        print("| player | " + " | ".join(maps) + " | all |")
        print("| --- |" + " --- |" * (len(maps) + 1))
        print("| teacher | " + " | ".join(f"{100 * t['maps'][m]['rate']:.0f}%" for m in maps)
              + f" | {100 * t['all']['rate']:.0f}% |")
        for stage in ("bc", "dagger", "ppo"):
            for how in ("greedy", "sampled"):
                files = sorted(folder.glob(f"{args.tag}_{stage}_s*_{where}_{how}.json"))
                runs = [json.loads(f.read_text()) for f in files]
                if not runs:
                    continue
                seeds = len(runs)
                row = [cell([r["maps"][m]["rate"] for r in runs if m in r["maps"]]) for m in maps]
                row.append(cell([r["all"]["rate"] for r in runs]))
                print(f"| {stage}, {how} ({seeds} seed{'s' if seeds > 1 else ''}) | " + " | ".join(row) + " |")
