# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Quake 2 as an environment: one server process per player, stepped one
server frame (100 ms) at a time."""

from .engine import Engine, EngineError
from .env import Q2Env, Q2VecEnv, observation_space, action_space
from . import layout

__all__ = ["Engine", "EngineError", "Q2Env", "Q2VecEnv", "observation_space",
           "action_space", "layout"]
