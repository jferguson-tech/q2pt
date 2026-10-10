# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Packs a folder of .planes files as .q2n files: see planes.pack.

    python repack.py <in folder> <out folder> [--burst N] [--delete]

Frames are frameNNNNN.planes. A frame gets a motion plane from the frame
before it when both are of one burst: with --burst N (8 unless given, as
pt_walk makes them; 0 for a demo, where every frame follows the one before),
frame k follows k-1 unless k is a multiple of N. With --delete each .planes
file is removed once its .q2n is written and read back.
"""
import os
import sys
import time

import planes


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, dst = argv[1], argv[2]
    burst = 8
    delete = "--delete" in argv
    if "--burst" in argv:
        burst = int(argv[argv.index("--burst") + 1])
    os.makedirs(dst, exist_ok=True)
    names = sorted(n for n in os.listdir(src) if n.startswith("frame") and n.endswith(".planes"))
    total_in = total_out = 0
    t0 = time.time()
    prev_meta, prev_index = None, None
    for n in names:
        index = int(n[5:10])
        path = os.path.join(src, n)
        out = os.path.join(dst, n[:-7] + ".q2n")
        follows = prev_index is not None and index == prev_index + 1 and (burst <= 0 or index % burst != 0)
        written = planes.pack(path, out, prev_meta if follows else None)
        p, meta = planes.read(out)
        if "depth" not in p or (follows and "motion" not in p):
            raise RuntimeError(f"{out} did not read back")
        total_in += os.path.getsize(path)
        total_out += written
        prev_meta, prev_index = meta, index
        if delete:
            os.remove(path)
    n = max(len(names), 1)
    print(f"{len(names)} frames: {total_in / 1e6 / n:.1f} MB each in, {total_out / 1e6 / n:.1f} MB each out, "
          f"{total_out / 1e9:.2f} GB in all, {time.time() - t0:.0f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
