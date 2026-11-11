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


# The game's item numbers (itemlist in game/g_items.c) that a lean start
# touches: the armours that are worn, the weapons that can be lost (the
# blaster cannot), and the ammunition, hand grenades among it.
_ARMOUR = (1, 2, 3)
_WEAPONS = (8, 9, 10, 11, 13, 14, 15, 16, 17)
_AMMO = (12, 18, 19, 20, 21, 22)
_BLASTER = 7


def draw(leg, seed, lean=0.0):
    """The row an episode of this seed starts with.

    lean: the share of episodes that start with less than the teacher left
    with. A student that plays the map before on its own account arrives
    with less than the teacher does (report 9: without the shotgun, on the
    one map measured), and a student trained only from the teacher's rows
    has never had to make do. In a lean start the health is cut to between
    a half and all of the row's, the armour and each kind of ammunition to
    between none and all, and each weapon but the blaster is lost one time
    in two."""
    rng = np.random.default_rng(seed)
    r = rows(leg)
    row = r[int(rng.integers(len(r)))]
    if lean <= 0 or rng.random() >= lean:
        return row
    row = row.copy()
    inv = row[L.CARRY_INVENTORY:]
    row[L.CARRY_HEALTH] = max(25, int(round(row[L.CARRY_HEALTH] * rng.uniform(0.5, 1))))
    for i in _ARMOUR + _AMMO:
        inv[i] = int(inv[i] * rng.uniform(0, 1))
    for i in _WEAPONS:
        if rng.random() < 0.5:
            inv[i] = 0
    # the weapon in hand, and the one before it, must still be owned
    for k in (L.CARRY_WEAPON, L.CARRY_LAST):
        if row[k] > 0 and inv[row[k] - 1] == 0:
            row[k] = _BLASTER + 1
    return row


def keep(leg, kept):
    folder().mkdir(parents=True, exist_ok=True)
    np.save(path(leg), np.asarray(kept, np.int32).reshape(-1, L.CARRY_INTS))
    _cache.pop(leg, None)
