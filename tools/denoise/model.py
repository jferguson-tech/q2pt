# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The denoiser: a U-Net run frame after frame, each time also given its own
last answer moved to where things are now.

The game exports the light in the parts its tracer makes it in, and the
picture is

    albedo * diffuse + specular * mirrored + layers + exact

The network denoises the three noisy parts at once, each with the surface's
colour already divided out, so textures never pass through it; the exact
part does not pass through it either. It is told how noisy each part is at
each pixel.

A film is gone over twice. First from its end to its start, each frame given
the answer for the frame after it; then from the start, each frame given the
answer for the frame before it and the first pass's answer for the frame
after. What is returned for each part is a blend of a fresh estimate and
those neighbours' answers, so every frame draws on the paths of the frames
on both sides of it, which is also what keeps a film steady.
"""
import math

import torch
import torch.nn as nn
import torch.nn.functional as F

PARTS = 3                # diffuse, mirrored, layers
IN_CHANNELS = 9 + 3 + 3 + 3 + 3 + 3 + 1 + 1 + (9 + 1 + 1) * 2
OUT_CHANNELS = 9 + PARTS * 2


def squash(x):
    """light of any strength into a range a network likes"""
    return torch.log1p(torch.clamp(x, min=0.0) * 16.0) * 0.25


def unsquash(y):
    return torch.expm1(torch.clamp(y, min=0.0, max=6.0) * 4.0) / 16.0


def luminance(c):
    return 0.2126 * c[:, 0:1] + 0.7152 * c[:, 1:2] + 0.0722 * c[:, 2:3]


def picture(light, albedo, specular, exact):
    return albedo * light[:, 0:3] + specular * light[:, 3:6] + light[:, 6:9] + exact


def warp(image, motion):
    """image [B,C,H,W] of the last frame, brought to this frame: motion [B,2,H,W]
    is, in pixels, where each pixel was less where it is"""
    b, _, h, w = image.shape
    ys, xs = torch.meshgrid(torch.arange(h, device=image.device, dtype=image.dtype),
                            torch.arange(w, device=image.device, dtype=image.dtype), indexing='ij')
    px = xs[None] + motion[:, 0]
    py = ys[None] + motion[:, 1]
    inside = ((px >= 0) & (px <= w - 1) & (py >= 0) & (py <= h - 1)).unsqueeze(1)
    grid = torch.stack([(px + 0.5) / w * 2 - 1, (py + 0.5) / h * 2 - 1], dim=-1)
    out = F.grid_sample(image, grid, mode='bilinear', padding_mode='border', align_corners=False)
    return out, inside.to(image.dtype)


def depth_feature(depth):
    return torch.where(depth > 0, torch.log2(1.0 + torch.clamp(depth, min=0.0)) / 12.0, torch.zeros_like(depth))


class Block(nn.Module):
    def __init__(self, cin, cout):
        super().__init__()
        self.a = nn.Conv2d(cin, cout, 3, padding=1)
        self.b = nn.Conv2d(cout, cout, 3, padding=1)

    def forward(self, x):
        return F.relu(self.b(F.relu(self.a(x))))


class UNet(nn.Module):
    def __init__(self, cin=IN_CHANNELS, cout=OUT_CHANNELS, widths=(48, 96, 128, 192, 256)):
        super().__init__()
        self.down = nn.ModuleList()
        c = cin
        for w in widths:
            self.down.append(Block(c, w))
            c = w
        self.up = nn.ModuleList()
        for w in reversed(widths[:-1]):
            self.up.append(Block(c + w, w))
            c = w
        self.out = nn.Conv2d(c, cout, 3, padding=1)

    def forward(self, x):
        skips = []
        for i, block in enumerate(self.down):
            if i:
                x = F.max_pool2d(x, 2)
            x = block(x)
            skips.append(x)
        skips.pop()
        for block in self.up:
            x = F.interpolate(x, scale_factor=2, mode='nearest')
            x = block(torch.cat([x, skips.pop()], dim=1))
        return self.out(x)


class Denoiser(nn.Module):
    """One step: a frame in, its three lights denoised out, and the state for the next."""

    def __init__(self):
        super().__init__()
        self.net = UNet()

    def forward(self, f, paths, scale, state=None, ahead=None, backwards=False):
        """f: the frame, a dict of [B,C,H,W] as ptx.inputs gives it, with the
        motion to the frame after ('onward', 'onward_known') beside the motion
        to the frame before. paths [B]: paths a pixel in its light. scale [B]:
        what brings the light to a usual brightness (network_scale).
        state: what the last call returned, or None at the start of a film or
        after a cut. Going backwards the last call was for the frame after.
        ahead: going forwards, what the backward pass returned for the frame
        after, or None.
        Returns the denoised lights [B,9,H,W] and the new state."""
        light, depth, normal = f['light'], f['depth'], f['normal']
        s = scale.view(-1, 1, 1, 1)
        lit = light * s
        depth_f = depth_feature(depth)

        def bring(other, motion, known):
            """another frame's answer, moved to this frame: the light, where there is any, and how far its depth is off"""
            if other is None:
                return torch.zeros_like(lit), torch.zeros_like(depth), torch.zeros_like(depth)
            both, inside = warp(torch.cat([other[0], other[1]], dim=1), motion)
            have = inside * known
            return both[:, 0:9] * s * have, have, torch.clamp((both[:, 9:10] - depth_f).abs() * 20.0, max=1.0) * have

        if backwards:
            last, have, depth_gap = bring(state, f['onward'], f['onward_known'])
        else:
            last, have, depth_gap = bring(state, f['motion'], f['known'])
        next_, have_next, next_gap = bring(ahead, f['onward'], f['onward_known'])

        # how unsure each part is at each pixel, against how bright it is
        lum = torch.cat([luminance(lit[:, c * 3:c * 3 + 3]) for c in range(PARTS)], dim=1)
        noise = torch.sqrt(torch.clamp(f['variance'], min=0.0)) * s / (lum + 0.01)
        noise = torch.clamp(noise, max=8.0) * 0.25

        base = squash(lit)
        paths_f = (torch.log2(paths.float()) / 4.0).view(-1, 1, 1, 1).expand_as(depth)
        # the exact light is not touched, but says where lamps and the sky are
        x = torch.cat([base, noise, f['albedo'], f['specular'], squash(f['exact'] * s),
                       normal, depth_f, paths_f, squash(last), have, depth_gap,
                       squash(next_), have_next, next_gap], dim=1)
        y = self.net(x.to(memory_format=torch.channels_last)).float()

        fresh = unsquash(y[:, 0:9] + base.float())
        # shares of the neighbours' answers, against one of the fresh estimate
        a = (torch.exp(torch.clamp(y[:, 9:12], -15.0, 15.0)) * have).repeat_interleave(3, dim=1)
        b = (torch.exp(torch.clamp(y[:, 12:15], -15.0, 15.0)) * have_next).repeat_interleave(3, dim=1)
        out = (fresh + last.float() * a + next_.float() * b) / (1.0 + a + b) / s
        return out, (out, depth_f)


def widen(weights):
    """weights of the network that only looked back, made to fit this one:
    it starts by ignoring the frame after"""
    w = dict(weights)
    first, last, bias = 'net.down.0.a.weight', 'net.out.weight', 'net.out.bias'
    if w[first].shape[1] < IN_CHANNELS:
        w[first] = torch.cat([w[first], w[first].new_zeros(w[first].shape[0], IN_CHANNELS - w[first].shape[1], 3, 3)], dim=1)
    if w[last].shape[0] < OUT_CHANNELS:
        more = OUT_CHANNELS - w[last].shape[0]
        w[last] = torch.cat([w[last], w[last].new_zeros(more, *w[last].shape[1:])])
        w[bias] = torch.cat([w[bias], w[bias].new_full((more,), -4.0)])
    return w


def pad_to(x, multiple=16):
    h, w = x.shape[-2:]
    ph, pw = (-h) % multiple, (-w) % multiple
    return F.pad(x, (0, pw, 0, ph), mode='replicate') if (ph or pw) else x


# ---- the game's way of putting light on a screen, for losses and pictures

TYPICAL_TARGET = 0.0054      # pt/cpu/pt_cpu.cpp


def exposure_for(typical, setting=2.0):
    return setting * min(16.0, max(0.125, TYPICAL_TARGET / max(typical, 1e-6)))


ADAPT = 2.5                  # pt/cpu/pt_cpu.cpp: how fast the game's exposure follows the scene, a second


def adapted(typicals, times):
    """The game's exposure at setting 1 for each frame of a shot, from each
    frame's typical brightness and time in seconds. As in the game it is
    measured on one frame and used from the next, and follows over about a
    second: a flash is shown at the exposure of the dark before it."""
    out, e = [], None
    for i, typical in enumerate(typicals):
        want = exposure_for(typical, 1.0)
        dt = times[i] - times[i - 1] if i else 0.0
        if e is None or dt < 0.0 or dt > 1.0:
            e = want
        out.append(e)
        e += (want - e) * (1.0 - math.exp(-dt * ADAPT)) if i else 0.0
    return out


def network_scale(typical):
    """What the network's light is multiplied by, from the picture's typical
    brightness. It goes by the game's exposure and has the same limits, so
    the network sees light much as it will be shown; the 3 is for the
    surface colour, which the picture has in it and the light has not."""
    return 3.0 * exposure_for(typical, 1.0)


def display(c, exposure, training=False):
    """linear light to what is shown: exposure, the filmic curve, gamma"""
    x = torch.clamp(c * exposure, min=0.0)
    y = torch.clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)
    if training:
        # the small addition keeps the slope finite at black
        return (y + 1e-3).pow(1.0 / 2.2) - 1e-3 ** (1.0 / 2.2)
    return y.pow(1.0 / 2.2)
