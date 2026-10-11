# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Reads the navigation graph the game builds of a map (game/g_rl_nav.c)."""

import numpy as np

from .engine import data_dir

MAGIC = 0x3156414E
VERSION = 9
WALK, JUMP, DUCK, SWIM, CLIMB, RIDE = range(6)
LINK_NAMES = ("walk", "jump", "duck", "swim", "climb", "ride")
NODE_WATER, NODE_DUCK, NODE_MOVER, NODE_HIGH = 1, 2, 4, 8

NODE = np.dtype([("origin", "<i2", (3,)), ("flags", "u1"), ("pad", "u1"), ("ent", "<i2"),
                 ("pad2", "<i2"), ("first_link", "<i4"), ("num_links", "<i4")])
LINK = np.dtype([("from", "<i4"), ("to", "<i4"), ("cost", "<f4"), ("type", "u1"),
                 ("state", "u1"), ("ent", "<i2"),
                 ("heading", "u1"), ("steps", "u1"), ("pad", "<i2")])


class Nav:
    def __init__(self, map):
        path = data_dir() / "nav" / f"{map}.nav"
        with open(path, "rb") as f:
            magic, version, nodes, links = np.fromfile(f, "<i4", 4)
            if magic != MAGIC or version != VERSION:
                raise ValueError(f"{path} is not a version {VERSION} navigation graph")
            self.nodes = np.fromfile(f, NODE, nodes)
            self.links = np.fromfile(f, LINK, links)
        # the player's origin at each node, in the map's units
        self.pos = self.nodes["origin"].astype(np.float32) / 8
