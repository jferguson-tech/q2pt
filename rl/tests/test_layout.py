# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The shared block here and in game/g_rl.h must be the same, field for field.

Compiles a few lines of C that print the header's offsets and constants and
compares them with q2env/layout.py. Needs a C compiler.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import layout as L  # noqa: E402

REPO = Path(__file__).resolve().parents[2]

CONSTANTS = {
    "RL_MAGIC": L.MAGIC, "RL_VERSION": L.VERSION, "RL_STEP_MSEC": L.STEP_MSEC,
    "RL_REQ_RESET": L.REQ_RESET, "RL_REQ_STEP": L.REQ_STEP, "RL_REQ_QUIT": L.REQ_QUIT,
    "RL_DONE_EXIT": L.DONE_EXIT, "RL_DONE_DEATH": L.DONE_DEATH, "RL_DONE_TIME": L.DONE_TIME,
    "RL_ACT_BRANCHES": L.ACT_BRANCHES, "RL_YAW_BINS": L.YAW_BINS,
    "RL_PITCH_BINS": L.PITCH_BINS, "RL_WEAPONS": L.WEAPONS,
    "RL_RAYS_X": L.RAYS_X, "RL_RAYS_Y": L.RAYS_Y, "RL_HIT_KINDS": L.HIT_KINDS,
    "RL_ENTS": L.ENTS, "RL_ENT_FLOATS": L.ENT_FLOATS, "RL_KINDS": L.KINDS,
    "RL_SELF_FLOATS": L.SELF_FLOATS, "RL_GUIDE_FLOATS": L.GUIDE_FLOATS,
    "RL_GAIN_FLOATS": L.GAIN_FLOATS, "RL_GAIN_PROGRESS": L.GAIN_PROGRESS,
    "RL_GAIN_DEALT": L.GAIN_DEALT, "RL_GAIN_TAKEN": L.GAIN_TAKEN, "RL_GAIN_KILLS": L.GAIN_KILLS,
    "(int)RL_FOV_X": int(L.FOV_X), "(int)RL_FOV_Y": int(L.FOV_Y),
    "(int)RL_RAY_RANGE": int(L.RAY_RANGE),
}


class Layout(unittest.TestCase):
    def test_same_as_header(self):
        lines = ['#include <stdio.h>', '#include <stddef.h>',
                 f'#include "{REPO / "game" / "g_rl.h"}"', 'int main (void) {',
                 'printf ("size %d\\n", (int)sizeof(rl_shared_t));']
        for name in L.SHARED.names:
            lines.append(f'printf ("{name} %d %d\\n", (int)offsetof(rl_shared_t, {name}),'
                         f' (int)sizeof(((rl_shared_t *)0)->{name}));')
        for name in CONSTANTS:
            lines.append(f'printf ("const %d\\n", (int)({name}));')
        lines.append('return 0; }')

        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "layout.c", Path(tmp) / "layout"
            src.write_text("\n".join(lines))
            subprocess.run(["cc", "-o", str(exe), str(src)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split("\n")

        self.assertEqual(out[0], f"size {L.SHARED.itemsize}")
        for line, name in zip(out[1:], L.SHARED.names):
            dtype, offset = L.SHARED.fields[name][:2]
            self.assertEqual(line, f"{name} {offset} {dtype.itemsize}")
        got = [int(x.split()[1]) for x in out[1 + len(L.SHARED.names):] if x]
        for (name, want), value in zip(CONSTANTS.items(), got, strict=True):
            self.assertEqual(value, want, name)


if __name__ == "__main__":
    unittest.main()
