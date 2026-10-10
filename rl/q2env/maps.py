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
