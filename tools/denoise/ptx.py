# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Reads the frames the game exports with pt_render_export (frameNNNNN.ptx).

The layout is described in ref_pt/rpt_export.c: a 256 byte header, then
planes of 16 bit floats. The picture is

    albedo * diffuse + specular * mirrored + layers + exact

where diffuse, mirrored and layers are noisy and the rest is not.
"""
import struct

import numpy as np

HEADER = 256
NO_MOTION = 20000.0      # anything this far is the game's mark for "not known"

# planes
SET = 12                 # a set: diffuse rgb, mirrored rgb, layers rgb, then the variance of each
EXACT, ALBEDO, SPECULAR, NORMAL, DEPTH, MOTION = 48, 51, 54, 57, 60, 61
ALL_LIGHT, ALL_PICTURE = 63, 72


def clean(a):
    return np.nan_to_num(np.asarray(a, dtype=np.float32), nan=0.0, posinf=65504.0, neginf=-65504.0)


def typical(c):
    """the geometric mean of the luminance of c [3, H, W]: what the game's own exposure goes by"""
    lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    return float(np.exp(np.mean(np.log(np.maximum(np.nan_to_num(lum), 0.0) + 1e-4))))


class Frame:
    def __init__(self, path):
        with open(path, 'rb') as f:
            b = f.read(HEADER)
        if b[:4] != b'PTXB':
            raise ValueError('%s is not an exported frame' % path)
        (self.version, self.width, self.height, self.planes, self.sets, self.set_paths,
         self.paths, self.flags, self.frame) = struct.unpack('<9i', b[4:40])
        if self.version != 2:
            raise ValueError('%s is version %d of the format; this reads version 2' % (path, self.version))
        self.time, self.blur = struct.unpack('<2f', b[40:48])
        cam = struct.unpack('<14f', b[48:104])
        self.origin, self.forward, self.right, self.up = (np.array(cam[i:i + 3], np.float32) for i in (0, 3, 6, 9))
        self.fov = cam[12:14]
        self.follows = bool(self.flags & 1)      # the motion plane means something
        self.blurred = bool(self.flags & 2)
        self.path = path
        self.data = np.memmap(path, dtype='<f2', mode='r', offset=HEADER,
                              shape=(self.planes, self.height, self.width))

    def crop(self, first, count, box):
        y0, y1, x0, x1 = box
        return clean(self.data[first:first + count, y0:y1, x0:x1])

    def box(self, box=None):
        return (0, self.height, 0, self.width) if box is None else box


def inputs(frame, sets, box=None, light_from_all=False):
    """What the denoiser is given for a frame, float32 arrays [C, H, W]:
      light     9: diffuse, mirrored and layers, averaged over the given sets
      variance  3: of that average's brightness, one for each of the three
      exact, albedo, specular, normal (as the eye sees it), depth,
      motion (pixels, to the last frame), known (whether the motion is)"""
    box = frame.box(box)
    acc = None
    for s in sets:
        a = frame.crop(s * SET, SET, box)
        acc = a if acc is None else acc + a
    acc /= len(sets)
    light = acc[0:9]
    # the planes hold the variance of one path; the average is of this many
    variance = acc[9:12] / (len(sets) * frame.set_paths)
    if light_from_all:
        # more paths than the sets hold were asked of the game: use them all
        light = frame.crop(ALL_LIGHT, 9, box)
        variance = variance * (len(sets) * frame.set_paths / max(frame.paths, 1))
    once = frame.crop(EXACT, 15, box)
    exact, albedo, specular, n = once[0:3], once[3:6], once[6:9], once[9:12]
    normal = np.stack([np.tensordot(frame.right, n, 1), np.tensordot(frame.up, n, 1), -np.tensordot(frame.forward, n, 1)])
    depth = once[12:13]
    motion = once[13:15].copy()
    known = (np.abs(motion[0:1]) < NO_MOTION) & frame.follows
    motion[:, ~known[0]] = 0
    if frame.blurred:
        # from the middle of one open shutter to the middle of the last
        motion /= max(1.0 - frame.blur * 0.5, 0.25)
    return dict(light=light, variance=variance, exact=exact, albedo=albedo, specular=specular, normal=normal,
                depth=depth, motion=motion, known=known.astype(np.float32))


def picture(light, albedo, specular, exact):
    """the parts put together; works on numpy arrays and on torch tensors, [..., C, H, W]"""
    return albedo * light[..., 0:3, :, :] + specular * light[..., 3:6, :, :] + light[..., 6:9, :, :] + exact


def reference(frame, box=None):
    """from all the paths asked of the game: the three lights [9, H, W] and the picture [3, H, W]"""
    a = frame.crop(ALL_LIGHT, 12, frame.box(box))
    return a[0:9], a[9:12]


def scales(light, step=8):
    """what brings each of the three lights to a usual brightness, for the network"""
    return np.array([0.05 / typical(light[c * 3:c * 3 + 3, ::step, ::step]) for c in range(3)], np.float32)
