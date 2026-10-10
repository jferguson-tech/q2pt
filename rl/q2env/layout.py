# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The block of memory shared with the game, as game/g_rl.h declares it.

Change the two together and raise VERSION. tests/test_layout.py compiles the
header and compares every offset and constant with what is here.
"""

import numpy as np

MAGIC = 0x314C5251
VERSION = 3
STEP_MSEC = 100

REQ_RESET, REQ_STEP, REQ_QUIT = 1, 2, 3
# for testing navigation only, never for a run that counts as play
FLAG_NOTARGET = 1       # monsters take no notice of the player
FLAG_NOMONSTERS = 2     # the map's monsters are taken out
DONE_NO, DONE_EXIT, DONE_DEATH, DONE_TIME = 0, 1, 2, 3
DONE_NAMES = ("running", "exit", "death", "time")

# the action: one choice in each branch
ACT_FORWARD, ACT_STRAFE, ACT_UP, ACT_YAW, ACT_PITCH, ACT_FIRE, ACT_WEAPON = range(7)
ACT_BRANCHES = 7
YAW_BINS = 15
PITCH_BINS = 11
WEAPONS = 10
ACT_SIZES = (3, 3, 3, YAW_BINS, PITCH_BINS, 2, WEAPONS + 1)
# the action that does nothing
ACT_IDLE = (1, 1, 1, YAW_BINS // 2, PITCH_BINS // 2, 0, 0)

RAYS_X, RAYS_Y = 24, 9
RAYS = RAYS_X * RAYS_Y
FOV_X, FOV_Y = 90.0, 60.0
RAY_RANGE = 2048.0
HIT_KINDS = 10

ENTS = 16
ENT_FLOATS = 12
KINDS = 15

SELF_FLOATS = 32
GUIDE_FLOATS = 4
GAIN_FLOATS = 8
GAIN_PROGRESS, GAIN_DEALT, GAIN_TAKEN, GAIN_KILLS = 0, 1, 2, 3

SHARED = np.dtype([
    ("magic", "<i4"), ("version", "<i4"), ("size", "<i4"),
    # written here
    ("request", "<i4"),
    ("action", "<i4", (ACT_BRANCHES,)),
    ("act_teacher", "<i4"),
    ("seed", "<i4"), ("skill", "<i4"), ("time_limit", "<i4"), ("flags", "<i4"),
    ("map", "S64"), ("demo", "S256"),
    # written by the game and the server
    ("error", "<i4"), ("error_text", "S128"),
    ("step", "<i4"), ("done", "<i4"), ("hash", "<u4"),
    ("demo_frames", "<i4"), ("demo_dropped", "<i4"),
    ("teacher", "<i4", (ACT_BRANCHES,)),
    ("gain", "<f4", (GAIN_FLOATS,)),
    ("self", "<f4", (SELF_FLOATS,)),
    ("guide", "<f4", (GUIDE_FLOATS,)),
    ("ray_dist", "<f4", (RAYS,)),
    ("ray_slope", "<f4", (RAYS,)),
    ("ray_kind", "<i4", (RAYS,)),
    ("ents", "<f4", (ENTS, ENT_FLOATS)),
    # not for the player
    ("origin", "<f4", (3,)), ("angles", "<f4", (3,)),
    ("monsters_total", "<i4"), ("monsters_killed", "<i4"),
    ("nav_count", "<i4"), ("nav_node", "<i4"), ("nav_goal", "<i4"),
    ("goals_reached", "<i4"), ("goals_failed", "<i4"), ("restart", "<i4"),
    ("link_type", "<i4"), ("link_ent", "<i4"), ("route_left", "<f4"),
])

# the fields that make up what the player perceives
OBS_FIELDS = ("self", "guide", "ray_dist", "ray_slope", "ray_kind", "ents")
