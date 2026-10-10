# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The dataset's split, by map: whole maps are held out for validation and
for test, and nothing of them is used for training. The shipped demos play
on training maps.

    python split.py            lists the three sets
    python split.py <map>      says which set a map is in
"""
import sys

VALIDATION = ["jail3", "mine2", "waste1", "q2dm3"]
TEST = ["fact2", "ware2", "city1", "q2dm1"]
TRAIN = [
    "base1", "base2", "base3", "biggun", "boss1", "boss2", "bunk1", "city2", "city3", "command",
    "cool1", "fact1", "fact3", "hangar1", "hangar2", "jail1", "jail2", "jail4", "jail5", "lab",
    "mine1", "mine3", "mine4", "mintro", "power1", "power2", "q2dm2", "q2dm4", "q2dm5", "q2dm6",
    "q2dm7", "q2dm8", "security", "space", "strike", "train", "ware1", "waste2", "waste3",
]
DEMOS = ["demo1", "demo2"]		# recorded on training maps


def set_of(name):
    """train, validation or test for a map's name, with or without maps/ and .bsp."""
    name = name.split("/")[-1].removesuffix(".bsp")
    if name in VALIDATION:
        return "validation"
    if name in TEST:
        return "test"
    if name in TRAIN:
        return "train"
    raise KeyError(f"{name} is in no set")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        print(set_of(sys.argv[1]))
    else:
        for label, maps in (("train", TRAIN), ("validation", VALIDATION), ("test", TEST)):
            print(f"{label} ({len(maps)}): {' '.join(maps)}")
