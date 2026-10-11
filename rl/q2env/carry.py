# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""What the player carries from one map of a unit into the next.

When an episode ends at the exit that leads on, the game reports what the
player had: health, weapons, ammunition, keys (layout.CARRY_). The teacher's
are kept, one row for each time it got out, in a file per leg under
<data>/carry. An episode on the leg that follows starts with one of those
rows, picked by the episode's seed. tools/carry.py fills the files.
"""

import os
from pathlib import Path

import numpy as np

from . import layout as L

_cache = {}


def folder():
    return Path(os.environ.get("Q2PT_RL_DATA", Path.home() / "q2pt-rl")) / "carry"


def path(leg):
    return folder() / f"{leg}.npy"


def rows(leg):
    """Every row kept for the leg that was left: (n, CARRY_INTS)."""
    if leg not in _cache:
        if not path(leg).exists():
            raise FileNotFoundError(f"nothing carried out of {leg} yet: run rl/tools/carry.py")
        _cache[leg] = np.load(path(leg))
        assert _cache[leg].shape[1] == L.CARRY_INTS and len(_cache[leg])
    return _cache[leg]


def draw(leg, seed):
    """The row an episode of this seed starts with."""
    r = rows(leg)
    return r[int(np.random.default_rng(seed).integers(len(r)))]


def keep(leg, kept):
    folder().mkdir(parents=True, exist_ok=True)
    np.save(path(leg), np.asarray(kept, np.int32).reshape(-1, L.CARRY_INTS))
    _cache.pop(leg, None)
