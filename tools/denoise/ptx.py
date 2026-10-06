# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Reads the frames the game exports with pt_render_export (frameNNNNN.ptx).

The layout is described in ref_pt/rpt_export.c: a 256 byte header, then
planes of 16 bit floats.
"""
import struct

import numpy as np

HEADER = 256
NO_MOTION = 20000.0      # anything this far is the game's mark for "not known"

# planes
SET = 9                  # a set is light rgb, reflectance rgb, normal xyz
DEPTH, MOTION, LIGHT = 36, 37, 39


class Frame:
    def __init__(self, path):
        with open(path, 'rb') as f:
            b = f.read(HEADER)
        if b[:4] != b'PTXB':
            raise ValueError('%s is not an exported frame' % path)
        (self.version, self.width, self.height, self.planes, self.sets, self.set_paths,
         self.paths, self.flags, self.frame) = struct.unpack('<9i', b[4:40])
        self.time, self.blur = struct.unpack('<2f', b[40:48])
        cam = struct.unpack('<14f', b[48:104])
        self.origin, self.forward, self.right, self.up = (np.array(cam[i:i + 3], np.float32) for i in (0, 3, 6, 9))
        self.fov = cam[12:14]
        self.follows = bool(self.flags & 1)      # the motion plane means something
        self.blurred = bool(self.flags & 2)
        self.path = path
        self.data = np.memmap(path, dtype='<f2', mode='r', offset=HEADER,
                              shape=(self.planes, self.height, self.width))

    def crop(self, planes, y0, y1, x0, x1):
        return np.nan_to_num(np.asarray(self.data[planes, y0:y1, x0:x1], dtype=np.float32), nan=0.0, posinf=65504.0, neginf=-65504.0)

    def typical(self, sets=None, step=8):
        """the geometric mean of the luminance of the light: what the game's own exposure goes by"""
        sets = range(self.sets) if sets is None else sets
        c = sum(np.asarray(self.data[s * SET:s * SET + 3, ::step, ::step], dtype=np.float32) for s in sets) / len(sets)
        return typical(c)


def typical(c):
    """c [3, H, W] linear light"""
    lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    return float(np.exp(np.mean(np.log(np.maximum(np.nan_to_num(lum), 0.0) + 1e-4))))


def features(frame, sets, y0=0, y1=None, x0=0, x1=None, light_from_all=False):
    """What the denoiser is given for a frame, as float32 arrays [C, H, W]:
    light and reflectance and view space normal averaged over the given sets,
    distance, motion in pixels to the last frame and whether it is known."""
    y1 = frame.height if y1 is None else y1
    x1 = frame.width if x1 is None else x1
    acc = None
    for s in sets:
        a = frame.crop(slice(s * SET, s * SET + SET), y0, y1, x0, x1)
        acc = a if acc is None else acc + a
    acc /= len(sets)
    light, albedo, n = acc[0:3], acc[3:6], acc[6:9]
    if light_from_all:
        # more paths than the sets hold were asked of the game: use them all
        light = reference(frame, y0, y1, x0, x1)
    # the normal as the eye sees it: x right, y up, z towards the eye
    normal = np.stack([np.tensordot(frame.right, n, 1), np.tensordot(frame.up, n, 1), -np.tensordot(frame.forward, n, 1)])
    rest = frame.crop(slice(DEPTH, DEPTH + 3), y0, y1, x0, x1)
    depth = rest[0:1]
    motion = rest[1:3].copy()
    known = (np.abs(motion[0:1]) < NO_MOTION) & frame.follows
    motion[:, ~known[0]] = 0
    if frame.blurred:
        # from the middle of one open shutter to the middle of the last
        motion /= max(1.0 - frame.blur * 0.5, 0.25)
    return light, albedo, normal, depth, motion, known.astype(np.float32)


def reference(frame, y0=0, y1=None, x0=0, x1=None):
    y1 = frame.height if y1 is None else y1
    x1 = frame.width if x1 is None else x1
    return frame.crop(slice(LIGHT, LIGHT + 3), y0, y1, x0, x1)
