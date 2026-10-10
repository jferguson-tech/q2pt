# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The same map, seed and actions must give the same run, bit for bit.

A run is recorded as, for every step, the game's hash of the state of every
entity in the map and the bytes of everything the player perceives. It is
then repeated in the same server process, in a new process, and in a process
that has played another map and seed in between, and each repeat must match
the first at every step. A different seed must not.

    python -m unittest rl.tests.test_determinism        (from the repository)
"""

import sys
import unittest
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from q2env import Engine, layout as L  # noqa: E402

STEPS = 600
FIELDS = L.OBS_FIELDS + ("gain", "origin", "angles", "done", "step", "teacher")


def actions(seed, steps=STEPS):
    """Random actions that keep moving, so that the run covers ground and
    meets monsters: forward more often than not, fire a third of the time."""
    rng = np.random.default_rng(seed)
    a = np.stack([rng.integers(n, size=steps) for n in L.ACT_SIZES], axis=1).astype(np.int32)
    a[:, L.ACT_FORWARD] = np.where(rng.random(steps) < 0.7, 2, a[:, L.ACT_FORWARD])
    a[:, L.ACT_FIRE] = rng.random(steps) < 0.3
    return a


def run(engine, map, seed, acts):
    """The trace of one episode: a (hash, checksum of what was perceived) for
    the reset and for each step until the episode ends."""
    engine.reset(map, seed, skill=1, time_limit=len(acts))
    b = engine.block
    trace = []

    def note():
        crc = 0
        for k in FIELDS:
            crc = zlib.crc32(np.ascontiguousarray(b[k]).tobytes(), crc)
        trace.append((int(b["hash"]), crc))

    note()
    for a in acts:
        if b["done"]:
            break
        engine.step(a)
        note()
    return trace


class Determinism(unittest.TestCase):
    def check(self, map):
        acts = actions(7)
        with Engine() as e:
            first = run(e, map, 123, acts)
            self.assertGreater(len(first), 50, "the run was too short to mean anything")
            again = run(e, map, 123, acts)
            self.assertEqual(first, again, "differs when repeated in the same process")

            other = run(e, map, 124, acts)
            self.assertNotEqual(first, other, "another seed gave the same run")
            run(e, "base2" if map != "base2" else "base1", 5, actions(8, 200))
            later = run(e, map, 123, acts)
            self.assertEqual(first, later, "differs after other episodes in the process")

        with Engine() as e:
            fresh = run(e, map, 123, acts)
        self.assertEqual(first, fresh, "differs in a new process")

    def test_base1(self):
        self.check("base1")

    def test_base2(self):
        self.check("base2")

    def test_bunk1(self):
        self.check("bunk1")

    def test_demo_does_not_change_the_run(self):
        """Recording a demo must not alter what happens."""
        import tempfile
        acts = actions(9, 300)
        with Engine() as e, tempfile.TemporaryDirectory() as tmp:
            plain = run(e, "base1", 42, acts)
            e.reset("base1", 42, skill=1, time_limit=len(acts), demo=str(Path(tmp) / "t.dm2"))
            hashes = [int(e.block["hash"])]
            for a in acts:
                if e.block["done"]:
                    break
                e.step(a)
                hashes.append(int(e.block["hash"]))
            self.assertEqual([h for h, _ in plain], hashes)


if __name__ == "__main__":
    unittest.main()
