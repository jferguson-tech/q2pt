# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Denoises a film rendered by the game with pt_render_export.

In the game:   pt_render_export 1
               pt_render <demo> 60 16
which leaves baseq2/render/<demo>/frameNNNNN.ptx. Then

    python pt_denoise.py baseq2/render/<demo> --weights denoiser.pt

writes frameNNNNN.png beside them, exposed and graded as the game would
have, ready for ffmpeg. The status bar is not in these frames: they are the
3D view only.
"""
import argparse, glob, math, os, sys

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

import ptx
from model import Denoiser, TYPICAL_TARGET, display, network_scale, pad_to, picture, widen


def load_model(weights, device):
    model = Denoiser().to(device).to(memory_format=torch.channels_last)
    ck = torch.load(weights, map_location=device)
    model.load_state_dict(widen(ck['model'] if 'model' in ck else ck))
    return model.eval()


def frame_paths(folder):
    return sorted(glob.glob(os.path.join(folder, 'frame*.ptx')))


def choose_sets(frame, paths):
    """which sets, and whether to use the light of all the paths instead"""
    if paths == 'all':
        return list(range(frame.sets)), frame.paths > frame.sets * frame.set_paths
    n = max(1, min(frame.sets, int(paths) // frame.set_paths))
    return list(range(n)), False


class Film:
    """Runs the denoiser over the frames of a film: a stretch at a time from
    its end to its start, then from its start to its end with what the first
    pass found for the frame after each."""

    STRETCH = 48         # frames gone over backwards at a time
    LEAD = 12            # frames past a stretch that the backward pass starts from
    STEADY = 8           # frames on each side that brightness is averaged over

    def __init__(self, model, device, paths='all', both_ways=True):
        self.model, self.device, self.paths, self.both_ways = model, device, paths, both_ways

    def load(self, frame, after):
        sets, from_all = choose_sets(frame, self.paths)
        a = ptx.inputs(frame, sets, light_from_all=from_all)
        if after is not None:
            a['onward'], a['onward_known'] = ptx.onward(after)
        else:
            a['onward'], a['onward_known'] = np.zeros_like(a['motion']), np.zeros_like(a['known'])
        count = frame.paths if from_all else len(sets) * frame.set_paths
        f = {k: pad_to(torch.from_numpy(np.ascontiguousarray(v)).to(self.device)[None]) for k, v in a.items()}
        return f, torch.tensor([min(count, 16)], device=self.device)

    def step(self, f, count, scale, state, ahead=None, backwards=False):
        with torch.autocast(self.device.type, dtype=torch.bfloat16, enabled=self.device.type == 'cuda'):
            return self.model(f, count, scale, state, ahead, backwards)

    @torch.no_grad()
    def run(self, frames):
        """frames: ptx.Frame, in order. Yields for each the denoised picture
        [3,H,W] as linear light, and the motion [2,H,W] and known [1,H,W], on
        the device."""
        n = len(frames)
        # a cut, or a frame missing: nothing is carried across
        joined = [i > 0 and frames[i].follows and frames[i].frame == frames[i - 1].frame + 1 for i in range(n)]
        shot, shots = 0, []
        for i in range(n):
            shot += not joined[i]
            shots.append(shot)
        # brightness for the network: steady over a shot's neighbouring frames, so that it does not jump
        logs = [math.log(ptx.brightness(f, *choose_sets(f, self.paths))) for f in frames]
        scales = []
        for i in range(n):
            near = [logs[j] for j in range(max(0, i - self.STEADY), min(n, i + self.STEADY + 1)) if shots[j] == shots[i]]
            scales.append(torch.tensor([network_scale(math.exp(sum(near) / len(near)))], device=self.device))

        state = None
        for first in range(0, n, self.STRETCH):
            end = min(first + self.STRETCH, n)
            ahead = {}
            if self.both_ways:
                back = None
                for t in range(min(end + self.LEAD, n) - 1, first - 1, -1):
                    after = frames[t + 1] if t + 1 < n and joined[t + 1] else None
                    if after is None:
                        back = None
                    f, count = self.load(frames[t], after)
                    _, back = self.step(f, count, scales[t], back, backwards=True)
                    if first < t <= end and joined[t]:
                        ahead[t - 1] = (back[0].half(), back[1].half())
            for t in range(first, end):
                after = frames[t + 1] if t + 1 < n and joined[t + 1] else None
                if not joined[t]:
                    state = None
                f, count = self.load(frames[t], after)
                given = ahead.pop(t, None)
                if given is not None:
                    given = (given[0].float(), given[1].float())
                light, state = self.step(f, count, scales[t], state, given)
                h, w = frames[t].height, frames[t].width
                out = picture(light, f['albedo'], f['specular'], f['exact'])
                yield out[0, :, :h, :w].float(), f['motion'][0, :, :h, :w], f['known'][0, :, :h, :w]


def motion_blur(image, motion, share, taps=16):
    """What an open shutter would have made of a sharp frame: every pixel
    drawn out along the way it moved. share is the part of the frame's time
    the shutter is open."""
    c, h, w = image.shape
    ys, xs = torch.meshgrid(torch.arange(h, device=image.device, dtype=torch.float32),
                            torch.arange(w, device=image.device, dtype=torch.float32), indexing='ij')
    acc = torch.zeros_like(image)
    for k in range(taps):
        tau = share * (k + 0.5) / taps
        px, py = xs - motion[0] * tau, ys - motion[1] * tau
        grid = torch.stack([(px + 0.5) / w * 2 - 1, (py + 0.5) / h * 2 - 1], dim=-1)[None]
        acc += F.grid_sample(image[None], grid, mode='bilinear', padding_mode='border', align_corners=False)[0]
    return acc / taps


class Exposure:
    """The game's automatic exposure: brings the picture's typical brightness
    to a fixed level, following changes over about a second."""

    def __init__(self, setting=2.0, auto=True):
        self.setting, self.auto, self.value, self.time = setting, auto, None, None

    def __call__(self, light, time):
        if not self.auto:
            return self.setting
        typical = ptx.typical(light[:, 1::3, 1::3].cpu().numpy())
        want = min(16.0, max(0.125, TYPICAL_TARGET / max(typical, 1e-6)))
        dt = None if self.time is None else time - self.time
        if self.value is None or dt is None or dt < 0 or dt > 1:
            self.value = want
        else:
            self.value += (want - self.value) * (1 - math.exp(-dt * 2.5))
        self.time = time
        return self.setting * self.value


def to_png(shown, path):
    a = (shown.clamp(0, 1) * 255 + 0.5).byte().permute(1, 2, 0).cpu().numpy()
    Image.fromarray(a).save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder')
    ap.add_argument('--weights', required=True)
    ap.add_argument('--out', default='', help='where the pictures go; the same folder if not given')
    ap.add_argument('--paths', default='all', help="4, 8 or 16 to use only that many of the frame's paths; all by default")
    ap.add_argument('--exposure', type=float, default=2.0, help='the game\'s pt_exposure')
    ap.add_argument('--no-auto-exposure', action='store_true')
    ap.add_argument('--blur', type=float, default=0.0, help='add motion blur to sharp frames: share of the frame time the shutter is open')
    ap.add_argument('--past-only', action='store_true', help='one pass: each frame draws on those before it only; faster')
    ap.add_argument('--cpu', action='store_true')
    args = ap.parse_args()

    device = torch.device('cpu' if args.cpu or not torch.cuda.is_available() else 'cuda')
    model = load_model(args.weights, device)
    film = Film(model, device, args.paths, not args.past_only)
    exposure = Exposure(args.exposure, not args.no_auto_exposure)
    out = args.out or args.folder
    os.makedirs(out, exist_ok=True)
    paths = frame_paths(args.folder)
    if not paths:
        sys.exit('no frame*.ptx in %s' % args.folder)
    frames = [ptx.Frame(p) for p in paths]
    for i, (light, motion, known) in enumerate(film.run(frames)):
        frame = frames[i]
        if args.blur > 0 and not frame.blurred:
            light = motion_blur(light, motion * known, args.blur)
        shown = display(light, exposure(light, frame.time))
        to_png(shown, os.path.join(out, os.path.splitext(os.path.basename(frame.path))[0] + '.png'))
        print('\r%d / %d' % (i + 1, len(paths)), end='', flush=True)
    print()


if __name__ == '__main__':
    main()
