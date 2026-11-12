# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The entities of a map, read from the game data where it is installed.

Only what ties the maps of a unit together is wanted here: the exits
(target_changelevel) and the places the player comes in (info_player_start).
"""

import re
import struct

from .engine import run_dir

_ENTITIES_LUMP = 0


def _packed(name):
    base = run_dir() / "baseq2"
    loose = base / name
    if loose.exists():
        return loose.read_bytes()
    for pak in sorted(base.glob("pak*.pak"), reverse=True):
        with open(pak, "rb") as f:
            magic, offset, length = struct.unpack("<4sii", f.read(12))
            if magic != b"PACK":
                continue
            f.seek(offset)
            table = f.read(length)
            for i in range(0, length, 64):
                if table[i:i + 56].split(b"\0")[0].decode() == name:
                    at, size = struct.unpack("<ii", table[i + 56:i + 64])
                    f.seek(at)
                    return f.read(size)
    raise FileNotFoundError(name)


def entities(map):
    """A dict of keys and values for each entity of the map."""
    bsp = _packed(f"maps/{map}.bsp")
    offset, length = struct.unpack("<ii", bsp[8 + 8 * _ENTITIES_LUMP:16 + 8 * _ENTITIES_LUMP])
    text = bsp[offset:offset + length].decode("latin1")
    return [dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)) for block in re.findall(r"\{([^}]*)\}", text)]


def exits(map):
    """What the map's exits name, as the game is given it: "base3$base2a"."""
    return sorted({e["map"] for e in entities(map) if e.get("classname") == "target_changelevel" and "map" in e})


def leads_to(exit):
    """The map and the spawn point an exit's name stands for: a film may
    come first ("eou1_.cin+*bunk1$start"), and a star marks a new unit."""
    name = exit.split("+")[-1].lstrip("*")
    map, _, spawn = name.partition("$")
    return map.lower(), spawn.lower()
