# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The single-player maps, in the order the game is played through."""

ORDER = """base1 base2 base3 train bunk1 ware1 ware2 jail1 jail2 jail3 jail4 jail5 security
mintro mine1 mine2 mine3 mine4 fact1 fact2 fact3 power1 power2 cool1 waste1 waste2 waste3
biggun hangar1 hangar2 lab command strike space city1 city2 city3 boss1 boss2""".split()


def back_of(map):
    """The maps that come before this one, with spaces between: an exit to
    one of them is not the way on, and the teacher is told to leave it alone
    when the map has any other."""
    return " ".join(ORDER[:ORDER.index(map)]) if map in ORDER else ""


# A unit of the game is a few maps the player goes back and forth between.
# Played through, each visit to a map is a leg: it starts at the spawn point
# the exit just taken names, with what the player had on leaving, and ends at
# the exit that leads on. These are the legs of the shortest way through each
# unit that has been worked out, in order; a leg is named map@spawn, the
# first by its map alone.
#
# The base unit's side trip from base3 to train and back is left out: train
# has one exit, beside where the player comes in.
UNITS = {
    "base": (
        ("base1", "base1", "base2$base1"),
        ("base2@base1", "base2$base1", "base3$base2a"),
        ("base3@base2a", "base3$base2a", "base2$base3b"),
        ("base2@base3b", "base2$base3b", "eou1_.cin+*bunk1$start"),
    ),
}
LEGS = {name: (start, onward, legs[i - 1][0] if i else None)
        for legs in UNITS.values() for i, (name, start, onward) in enumerate(legs)}


def leg(name):
    """What an episode named so is: the map as the engine is to load it, the
    exit that counts (empty for any), the maps whose exits the teacher is to
    leave alone when no exit is named, and the leg before, whose teacher's
    leavings it starts with (None at the start of a unit, or for a map that
    is played alone)."""
    if name in LEGS:
        start, onward, before = LEGS[name]
        return start, onward, "", before
    return name, "", back_of(name.split("$")[0]), None


def bsp_of(name):
    """The map file a leg is played in."""
    return leg(name)[0].split("$")[0]
