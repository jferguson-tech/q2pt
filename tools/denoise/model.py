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
each pixel. What it returns for each part is a blend of a fresh estimate and
the last frame's answer, which is what keeps a film steady.
"""
import torch
import torch.nn as nn
import torch.nn.functional as F

PARTS = 3                # diffuse, mirrored, layers
IN_CHANNELS = 9 + 3 + 3 + 3 + 3 + 3 + 1 + 1 + 9 + 1 + 1


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
    def __init__(self, cin=IN_CHANNELS, cout=PARTS * 4, widths=(48, 96, 128, 192, 256)):
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

    def forward(self, f, paths, scales, state=None):
        """f: the frame, a dict of [B,C,H,W] as ptx.inputs gives it. paths [B]:
        paths a pixel in its light. scales [B,3]: what brings each of the
        three lights to a usual brightness. state: what the last call
        returned, or None at the start of a film or after a cut.
        Returns the denoised lights [B,9,H,W] and the new state."""
        light, depth, normal = f['light'], f['depth'], f['normal']
        # one scale for each colour plane of each part
        s9 = scales.repeat_interleave(3, dim=1).view(-1, 9, 1, 1)
        lit = light * s9
        depth_f = depth_feature(depth)

        if state is None:
            last = torch.zeros_like(lit)
            have = torch.zeros_like(depth)
            depth_gap = torch.zeros_like(depth)
        else:
            last_light, last_depth = state
            both, inside = warp(torch.cat([last_light, last_depth], dim=1), f['motion'])
            have = inside * f['known']
            last = both[:, 0:9] * s9 * have
            depth_gap = torch.clamp((both[:, 9:10] - depth_f).abs() * 20.0, max=1.0) * have

        # how unsure each part is at each pixel, against how bright it is
        lum = torch.cat([luminance(lit[:, c * 3:c * 3 + 3]) for c in range(PARTS)], dim=1)
        noise = torch.sqrt(torch.clamp(f['variance'], min=0.0)) * scales.view(-1, 3, 1, 1) / (lum + 0.01)
        noise = torch.clamp(noise, max=8.0) * 0.25

        base = squash(lit)
        paths_f = (torch.log2(paths.float()) / 4.0).view(-1, 1, 1, 1).expand_as(depth)
        # the exact light is not touched, but says where lamps and the sky are
        x = torch.cat([base, noise, f['albedo'], f['specular'], squash(f['exact'] * scales[:, 0].view(-1, 1, 1, 1)),
                       normal, depth_f, paths_f, squash(last), have, depth_gap], dim=1)
        y = self.net(x.to(memory_format=torch.channels_last)).float()

        fresh = unsquash(y[:, 0:9] + base.float())
        keep = torch.sigmoid(y[:, 9:12]).repeat_interleave(3, dim=1) * have
        out = (fresh * (1.0 - keep) + last.float() * keep) / s9
        return out, (out, depth_f)


def pad_to(x, multiple=16):
    h, w = x.shape[-2:]
    ph, pw = (-h) % multiple, (-w) % multiple
    return F.pad(x, (0, pw, 0, ph), mode='replicate') if (ph or pw) else x


# ---- the game's way of putting light on a screen, for losses and pictures

TYPICAL_TARGET = 0.0054      # pt/cpu/pt_cpu.cpp


def exposure_for(typical, setting=2.0):
    return setting * min(16.0, max(0.125, TYPICAL_TARGET / max(typical, 1e-6)))


def display(c, exposure, training=False):
    """linear light to what is shown: exposure, the filmic curve, gamma"""
    x = torch.clamp(c * exposure, min=0.0)
    y = torch.clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)
    if training:
        # the small addition keeps the slope finite at black
        return (y + 1e-3).pow(1.0 / 2.2) - 1e-3 ** (1.0 / 2.2)
    return y.pow(1.0 / 2.2)
