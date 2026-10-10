# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Renders the dataset: every map walked by pt_walk and the shipped demos,
with pt_render_export, each run of the game in turn, then packed.

    python render_dataset.py <game folder> <out folder> [--frames N] [--demo-frames N]
        [--paths N] [--maps a,b,c] [--no-demos] [--mode N]

The game folder holds quake2 and baseq2. Each map goes to
<out>/packed/<map>/frameNNNNN.q2n, each demo to <out>/packed/<demo>/; the
.planes files are packed as each run ends and removed. A log of every run
and its time is appended to <out>/render.log. Runs that already have their
packed folder are skipped, so the script can be run again after a stop.
"""
import os
import shutil
import subprocess
import sys
import time

import planes
import repack
import split


def run_game(game, args, log, limit):
    env = dict(os.environ)
    env.setdefault("DISPLAY", ":99")
    cmd = [os.path.join(game, "quake2"), "+set", "vid_ref", "ptrtx", "+set", "vid_fullscreen", "0",
           "+set", "pt_render_export", "1", "+set", "pt_render_hud", "0", "+set", "pt_render_blur", "0"] + args
    with open(log, "ab") as f:
        f.write((" ".join(cmd) + "\n").encode())
        f.flush()
        try:
            subprocess.run(cmd, cwd=game, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=limit)
        except subprocess.TimeoutExpired:
            f.write(b"timed out\n")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    game, out = argv[1], argv[2]
    opt = lambda name, default: type(default)(argv[argv.index(name) + 1]) if name in argv else default
    frames, demo_frames, paths, mode = opt("--frames", 200), opt("--demo-frames", 300), opt("--paths", 64), opt("--mode", 10)
    maps = opt("--maps", "").split(",") if "--maps" in argv else split.TRAIN + split.VALIDATION + split.TEST
    demos = [] if "--no-demos" in argv else split.DEMOS
    render = os.path.join(game, "baseq2", "render")
    packed = os.path.join(out, "packed")
    os.makedirs(packed, exist_ok=True)
    log = os.path.join(out, "render.log")

    jobs = [("walk", m) for m in maps] + [("demo", d) for d in demos]
    for kind, name in jobs:
        dst = os.path.join(packed, name)
        if os.path.isdir(dst) and any(n.endswith(".q2n") for n in os.listdir(dst)):
            print(f"{name}: already packed")
            continue
        src = os.path.join(render, ("walk_" if kind == "walk" else "") + name)
        shutil.rmtree(src, ignore_errors=True)
        t0 = time.time()
        if kind == "walk":
            run_game(game, ["+set", "gl_mode", str(mode), "+pt_walk", name, str(frames), str(paths), "30", "quit"], log, 3 * 3600)
        else:
            seconds = demo_frames / 30.0
            run_game(game, ["+set", "gl_mode", str(mode), "+pt_render", name, "30", str(paths), "1", f"{seconds:.2f}", "quit"],
                     log, 3 * 3600)
        took = time.time() - t0
        count = len([n for n in os.listdir(src) if n.endswith(".planes")]) if os.path.isdir(src) else 0
        line = f"{name}: {count} frames rendered in {took:.0f} s"
        print(line)
        with open(log, "a") as f:
            f.write(line + "\n")
        if count:
            repack.main(["repack", src, dst, "--burst", "8" if kind == "walk" else "0", "--delete"])
            shutil.rmtree(src, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
