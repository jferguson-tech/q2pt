# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Reads the .planes files the game writes with pt_render_export, checks them,
and makes contact sheets of them. See README.md for the format.

    python planes.py info   frame00000.planes
    python planes.py verify frame00000.planes [more...]
    python planes.py sheet  frame00000.planes out.png
"""
import struct
import sys
import zlib

import numpy as np

MAGIC = b"Q2PTPLNS"
TYPES = {0: np.float16, 1: np.float32, 2: np.uint8, 3: np.uint16, 4: np.uint32, 5: np.int32}


def read(path, planes=None):
    """The planes of a file as a dict of arrays (height, width, channels),
    and its text about the frame as a dict of strings. planes: names to
    read, or None for all."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != MAGIC:
        raise ValueError(f"{path}: not a planes file")
    version, width, height, count, table_at, meta_at, meta_len, data_at = struct.unpack_from("<8I", data, 8)
    if version != 1:
        raise ValueError(f"{path}: version {version} is not 1")
    out = {}
    for i in range(count):
        at = table_at + i * 48
        name = data[at:at + 24].split(b"\0", 1)[0].decode()
        kind, channels = struct.unpack_from("<II", data, at + 24)
        offset, nbytes = struct.unpack_from("<QQ", data, at + 32)
        if planes is not None and name not in planes:
            continue
        a = np.frombuffer(data, dtype=TYPES[kind], count=width * height * channels, offset=offset)
        out[name] = a.reshape(height, width, channels)
    meta = {}
    for line in data[meta_at:meta_at + meta_len].decode().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            meta[k.strip()] = v.strip()
    meta["_width"], meta["_height"] = width, height
    return out, meta


def vec(meta, key):
    return np.array([float(v) for v in meta[key].split()], dtype=np.float64)


def eye_dirs(meta):
    """The unit direction through the centre of every pixel, (height, width,
    3), as the tracer casts it with no offset within the pixel."""
    w, h = meta["_width"], meta["_height"]
    tx = np.tan(np.radians(float(meta["fov_x"]) * 0.5))
    ty = np.tan(np.radians(float(meta["fov_y"]) * 0.5))
    forward, right, up = vec(meta, "forward"), vec(meta, "right"), vec(meta, "up")
    px = (np.arange(w) + 0.5) / w
    py = (np.arange(h) + 0.5) / h
    sx = (2.0 * px - 1.0) * tx
    sy = (1.0 - 2.0 * py) * ty
    d = forward[None, None, :] + right[None, None, :] * sx[None, :, None] + up[None, None, :] * sy[:, None, None]
    return d / np.linalg.norm(d, axis=2, keepdims=True)


def luminance(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def verify(path, say=print):
    """Checks a file for what must hold: the picture is albedo times the
    diffuse light plus the specular light plus the rest, the surface
    positions lie on the eye rays at their depth, emission shows in the
    exact light, and the one-path light averages to the gathered light.
    Returns True if every check passes."""
    p, meta = read(path)
    ok = True
    f = lambda k: p[k].astype(np.float32)
    solid = p["depth"][..., 0] >= 0.0

    # the picture, put together from its parts
    rebuilt = f("albedo") / 255.0 * f("light_diffuse") + f("specular") / 255.0 * f("light_specular") \
        + f("light_layers") + f("light_extra")
    pic = f("picture")
    err = np.abs(rebuilt - pic).max(axis=2)
    scale = np.maximum(pic.max(axis=2), 1.0e-3)
    rel = (err / scale)[solid]
    bad = (rel > 0.05).mean()
    say(f"  picture = albedo*diffuse + specular*specular + layers + extra: median rel err {np.median(rel):.4f}, "
        f"over 5% in {bad * 100:.3f}% of solid pixels")
    # albedo is stored as a byte, which allows 1/255 of the light in error
    ok &= bad < 0.01

    # the surface lies on the eye ray
    d = eye_dirs(meta)
    origin = vec(meta, "origin")
    depth = f("depth")[..., 0].astype(np.float64)
    on_ray = origin[None, None, :] + d * depth[..., None]
    off = np.linalg.norm(on_ray - p["position"].astype(np.float64), axis=2)
    # what moves, and what is seen through water, is stored where it was or
    # where it appears: leave those out
    still = solid & (p["triangle"][..., 1] == 0)
    off_still = off[still]
    say(f"  position on the eye ray at depth: median {np.median(off_still):.4f} units, "
        f"over 0.5 in {(off_still > 0.5).mean() * 100:.3f}% of still pixels ({still.sum()} of {solid.sum()} solid)")
    ok &= (off_still > 0.5).mean() < 0.02

    # what glows shows in the exact light; the frame's point lights add to it
    glow = luminance(f("emission"))
    extra = luminance(f("light_extra"))
    lit = solid & (glow > 0.01)
    if lit.any():
        ratio = (extra[lit] / glow[lit])
        say(f"  extra / emission where it glows: median {np.median(ratio):.3f} (1 with no fog, less through fog), "
            f"below 0.9 in {(ratio < 0.9).mean() * 100:.2f}% of glowing pixels")
        ok &= np.median(ratio) > 0.2

    # one path's direct light is unbiased, if noisy
    ray, light = luminance(f("ray_diffuse")), luminance(f("light_diffuse"))
    s = solid & (light > 1.0e-3)
    say(f"  mean ray_diffuse / mean light_diffuse over solid pixels: {ray[s].mean() / light[s].mean():.3f} "
        f"(1 means the path's light is no brighter or darker than the gathered light; under 1 is expected: "
        f"the gathered light has bounces, the one path has none)")

    # the unshadowed direct light is never less than the shadowed one, bar noise
    direct = luminance(f("direct_diffuse")) / np.pi
    under = (direct[s] < 0.5 * light[s]).mean()
    say(f"  direct_diffuse/pi below half the gathered diffuse in {under * 100:.2f}% of lit solid pixels "
        f"(bounce light alone can do that; much more means a light is missing)")

    say(f"  clamped half floats: {meta.get('clamped', '?').strip()}; sky or nothing: {(~solid).mean() * 100:.1f}% of pixels")
    return bool(ok)


def tone(c, exposure=1.0):
    x = np.maximum(c * exposure, 0.0)
    y = x / (1.0 + x)
    return (np.power(y, 1.0 / 2.2) * 255.0 + 0.5).clip(0, 255).astype(np.uint8)


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\0" + rgb[y].tobytes() for y in range(h))

    def chunk(kind, body):
        c = kind + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def sheet(path, out, exposure=1.0, scale=2):
    """A contact sheet: three rows of four panels, each the picture reduced
    by scale."""
    p, meta = read(path)
    f = lambda k: p[k].astype(np.float32)
    depth = f("depth")[..., 0]
    far = np.percentile(depth[depth > 0], 99) if (depth > 0).any() else 1.0
    panels = [
        ("albedo", f("albedo") / 255.0),
        ("normal", f("normal") * 0.5 + 0.5),
        ("depth", np.repeat((np.clip(depth, 0, far) / far)[..., None], 3, axis=2)),
        ("emission", f("emission")),
        ("direct, unshadowed / pi", f("direct_diffuse") / np.pi * f("albedo") / 255.0),
        ("one ray, shadowed", f("ray_diffuse") * f("albedo") / 255.0),
        ("gathered diffuse", f("light_diffuse") * f("albedo") / 255.0),
        ("gathered specular", f("light_specular") * f("specular") / 255.0),
        ("layers", f("light_layers")),
        ("extra", f("light_extra")),
        ("picture", f("picture")),
        ("roughness, metallic, moving", np.stack([f("roughness")[..., 0] / 255.0, f("metallic")[..., 0] / 255.0,
                                                  (p["triangle"][..., 1] > 0).astype(np.float32)], axis=2)),
    ]
    h, w = depth.shape
    ph, pw = h // scale, w // scale
    rows, cols = 3, 4
    img = np.zeros((rows * ph, cols * pw, 3), dtype=np.uint8)
    for i, (name, c) in enumerate(panels):
        small = c[:ph * scale, :pw * scale].reshape(ph, scale, pw, scale, 3).mean(axis=(1, 3))
        linear = name in ("normal", "depth", "roughness, metallic, moving", "albedo")
        tile = (np.clip(small, 0, 1) ** (1 / 2.2) * 255 + 0.5).astype(np.uint8) if linear else tone(small, exposure)
        r, col = divmod(i, cols)
        img[r * ph:(r + 1) * ph, col * pw:(col + 1) * pw] = tile
    write_png(out, img)
    return [name for name, _ in panels]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, paths = argv[1], argv[2:]
    if cmd == "info":
        p, meta = read(paths[0])
        for k, v in meta.items():
            if not k.startswith("_"):
                print(f"{k} = {v[:100]}")
        for k, a in p.items():
            print(f"{k:16s} {a.dtype.name:8s} {a.shape}  min {float(a.min()):10.4f}  max {float(a.max()):10.4f}")
        return 0
    if cmd == "verify":
        good = True
        for path in paths:
            print(path)
            good &= verify(path)
        print("all checks passed" if good else "A CHECK FAILED")
        return 0 if good else 1
    if cmd == "sheet":
        names = sheet(paths[0], paths[1])
        print("panels, left to right, top to bottom:", "; ".join(names))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
