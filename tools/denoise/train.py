# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Trains the denoiser on frames made by render_dataset.py.

    python train.py --data /data/q2dn/train --out runs/a
"""
import argparse, glob, os, random, time

import numpy as np
import torch
import torch.nn.functional as F

import ptx
from model import Denoiser, display, exposure_for, luminance, network_scale, picture, warp, widen

KEYS = ('light', 'variance', 'exact', 'albedo', 'specular', 'normal', 'depth', 'motion', 'known', 'onward', 'onward_known')


class Clips(torch.utils.data.Dataset):
    """Stretches of consecutive frames, cut to a square, at 4, 8 or 16 paths a pixel."""

    def __init__(self, root, length=8, size=256, epoch=4000):
        self.clips = []
        for d in sorted(glob.glob(os.path.join(root, '*', 'c*'))):
            frames = sorted(glob.glob(os.path.join(d, '*.ptx')))
            if len(frames) >= 2:
                self.clips.append(frames)
        if not self.clips:
            raise SystemExit('no clips under %s' % root)
        self.length, self.size, self.epoch = length, size, epoch

    def __len__(self):
        return self.epoch

    def __getitem__(self, index):
        rng = random.Random()        # from the system: every pass over the data is different
        frames = rng.choice(self.clips)
        n = min(self.length, len(frames))
        first = rng.randrange(0, len(frames) - n + 1)
        opened = [ptx.Frame(p) for p in frames[first:first + n]]
        h, w = opened[0].height, opened[0].width
        s = self.size
        y0, x0 = rng.randrange(0, h - s + 1), rng.randrange(0, w - s + 1)
        box = (y0, y0 + s, x0, x0 + s)
        paths = rng.choice([4, 8, 16])
        flip = rng.random() < 0.5

        out = {k: [] for k in KEYS + ('ref_light', 'ref_picture')}
        for i, f in enumerate(opened):
            sets = rng.sample(range(f.sets), paths // f.set_paths)
            a = ptx.inputs(f, sets, box)
            a['ref_light'], a['ref_picture'] = ptx.reference(f, box)
            if i + 1 < n:
                a['onward'], a['onward_known'] = ptx.onward(opened[i + 1], box)
            else:
                a['onward'], a['onward_known'] = np.zeros_like(a['motion']), np.zeros_like(a['known'])
            if flip:
                a = {k: np.ascontiguousarray(v[:, :, ::-1]) for k, v in a.items()}
                a['normal'][0] *= -1
                a['motion'][0] *= -1
                a['onward'][0] *= -1
            for k in out:
                out[k].append(a[k])
        # brightness is judged on whole frames, as it is when a film is denoised, and for the clip as one
        step = 8
        logs = [np.log(ptx.typical(ptx.clean(f.data[ptx.ALL_PICTURE:ptx.ALL_PICTURE + 3, ::step, ::step])))
                for f in (opened[0], opened[n // 2], opened[-1])]
        typical = float(np.exp(np.mean(logs)))
        item = {k: torch.from_numpy(np.stack(v)) for k, v in out.items()}
        # a short clip is filled out by standing on its last frame
        if n < self.length:
            pad = self.length - n
            for k in item:
                item[k] = torch.cat([item[k], item[k][-1:].expand(pad, -1, -1, -1)])
            item['motion'][n:] = 0
            item['known'][n:] = 1
            item['onward'][n - 1:] = 0
            item['onward_known'][n - 1:-1] = 1
        item['paths'] = torch.tensor(paths)
        item['typical'] = torch.tensor(typical)
        return item


def gradients(x):
    return x[..., :, 1:] - x[..., :, :-1], x[..., 1:, :] - x[..., :-1, :]


def relative(x, y):
    """Squared error against the size of the answer. Unlike an absolute
    difference it is not pulled towards the middle of a noisy target, so a
    reference that still has noise in it teaches the right brightness. A
    target that is many times off (a stray very bright path) counts only so
    far."""
    return torch.clamp((x - y) ** 2 / (x.detach() + 0.02) ** 2, max=25.0).mean()


def haze(item, scale, paths, length):
    """Now and then, a coloured haze over a clip that comes and goes, thicker
    with distance and noisy as the tracer's own would be. Flashes that fill
    the screen are rare in the game but must not throw the denoiser."""
    b = item['light'].shape[0]
    device = item['light'].device
    for i in range(b):
        if random.random() > 0.15:
            continue
        first = random.randrange(length) if random.random() < 0.5 else 0
        last = random.randrange(first, length) if random.random() < 0.5 else length - 1
        colour = torch.rand(3, device=device) ** 2 + 0.02
        colour = colour / luminance(colour.view(1, 3, 1, 1)).view(()) * 0.05 * 2.0 ** random.uniform(-2.0, 5.0) / scale[i]
        depth = item['depth'][i, first:last + 1]
        through = torch.where(depth > 0, 1.0 - torch.exp(-depth * 2.0 ** random.uniform(-10.0, -5.0)), torch.ones_like(depth))
        clean = through * colour.view(1, 3, 1, 1)
        spread = 2.0 ** random.uniform(-3.0, 1.0) / float(paths[i])        # variance of the noisy haze against its strength
        grain = torch._standard_gamma(torch.full_like(through, 1.0 / spread)) * spread
        item['light'][i, first:last + 1, 6:9] += clean * grain
        item['variance'][i, first:last + 1, 2:3] += luminance(clean) ** 2 * spread
        item['ref_light'][i, first:last + 1, 6:9] += clean
        item['ref_picture'][i, first:last + 1] += clean


def measure(light, f, ref, ref_light, expo):
    """the losses that need one frame only: the picture, its parts, and how it is shown"""
    out = picture(light, f['albedo'], f['specular'], f['exact'])
    whole = relative(out * expo, ref * expo)
    # each part against its own reference, as it enters the picture
    apart = (relative(f['albedo'] * light[:, 0:3] * expo, f['albedo'] * ref_light[:, 0:3] * expo)
             + relative(f['specular'] * light[:, 3:6] * expo, f['specular'] * ref_light[:, 3:6] * expo)
             + relative(light[:, 6:9] * expo, ref_light[:, 6:9] * expo)) / 3
    shown, want = display(out, expo, True), display(ref, expo, True)
    gx, gy = gradients(shown)
    rx, ry = gradients(want)
    seen = F.l1_loss(shown, want) + 0.5 * (F.l1_loss(gx, rx) + F.l1_loss(gy, ry))
    return whole, apart, seen, shown, want


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--steps', type=int, default=40000)
    ap.add_argument('--batch', type=int, default=6)
    ap.add_argument('--length', type=int, default=8)
    ap.add_argument('--size', type=int, default=256)
    ap.add_argument('--lr', type=float, default=3e-4)
    ap.add_argument('--workers', type=int, default=12)
    ap.add_argument('--resume', default='')
    ap.add_argument('--start', default='', help='weights to begin from, of this network or of the one that only looked back')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    device = torch.device('cuda')
    torch.backends.cudnn.benchmark = True
    model = Denoiser().to(device).to(memory_format=torch.channels_last)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=args.lr, total_steps=args.steps, pct_start=0.03)
    step = 0
    if args.start:
        ck = torch.load(args.start, map_location=device)
        model.load_state_dict(widen(ck['model'] if 'model' in ck else ck))
    if args.resume:
        ck = torch.load(args.resume, map_location=device)
        model.load_state_dict(ck['model'])
        opt.load_state_dict(ck['opt'])
        sched.load_state_dict(ck['sched'])
        step = ck['step']

    data = Clips(args.data, args.length, args.size, epoch=args.batch * 1000)
    print('%d clips, %.2fM weights' % (len(data.clips), sum(p.numel() for p in model.parameters()) / 1e6), flush=True)
    loader = torch.utils.data.DataLoader(data, batch_size=args.batch, num_workers=args.workers, drop_last=True,
                                         persistent_workers=True, prefetch_factor=4)
    log = open(os.path.join(args.out, 'log.txt'), 'a')
    names = ('picture', 'parts', 'shown', 'change', 'backwards')
    start, sums, count = time.time(), np.zeros(5), 0
    while step < args.steps:
        for item in loader:
            item = {k: v.to(device, non_blocking=True) for k, v in item.items()}
            b = item['light'].shape[0]
            # brightness varies more between films than within this data
            jitter = torch.exp2(torch.empty(b, device=device).uniform_(-1.5, 1.5))
            j = jitter.view(-1, 1, 1, 1, 1)
            for k in ('light', 'exact', 'ref_light', 'ref_picture'):
                item[k] = item[k] * j
            item['variance'] = item['variance'] * j * j
            typical = item['typical'] * jitter
            expo = torch.tensor([exposure_for(t) for t in typical.tolist()], device=device).view(-1, 1, 1, 1)
            # the network's own scale is judged from the noisy frames in use, so not exactly
            scale = torch.tensor([network_scale(t) for t in typical.tolist()], device=device) \
                * torch.exp2(torch.empty(b, device=device).uniform_(-1.0, 1.0))
            haze(item, scale, item['paths'], args.length)
            frames = [{k: item[k][:, t] for k in KEYS} for t in range(args.length)]
            opt.zero_grad(set_to_none=True)

            # from the end to the start, each frame given the one after it
            state, back = None, torch.zeros((), device=device)
            ahead = [None] * args.length
            for t in reversed(range(args.length)):
                f = frames[t]
                with torch.autocast('cuda', dtype=torch.bfloat16):
                    light, state = model(f, item['paths'], scale, state, None, backwards=True)
                whole, apart, seen, _, _ = measure(light, f, item['ref_picture'][:, t], item['ref_light'][:, t], expo)
                back = back + whole + 0.5 * apart + 0.2 * seen
                # what the second pass is given is a fact to it, not something to change
                state = (state[0].detach(), state[1])
                if t:
                    ahead[t - 1] = state
            back = back / args.length
            (0.5 * back).backward()

            # from the start, each frame given the one before it and the first pass's answer for the one after
            if random.random() < 0.1:
                ahead = [None] * args.length         # it must still work alone
            state, last_out, last_ref = None, None, None
            parts = torch.zeros(5, device=device)
            for t in range(args.length):
                if t and random.random() < 0.05:
                    state = None                             # a cut: learn to start again
                f = frames[t]
                with torch.autocast('cuda', dtype=torch.bfloat16):
                    light, state = model(f, item['paths'], scale, state, ahead[t])
                whole, apart, seen, shown, want = measure(light, f, item['ref_picture'][:, t], item['ref_light'][:, t], expo)
                parts[0] += whole
                parts[1] += apart
                parts[2] += seen
                if last_out is not None:
                    # how the picture changes from frame to frame should be how the reference changes
                    moved, inside = warp(torch.cat([last_out, last_ref], dim=1), f['motion'])
                    ok = inside * f['known']
                    parts[3] += (((shown - moved[:, 0:3]) - (want - moved[:, 3:6])).abs() * ok).mean()
                last_out, last_ref = shown, want
            parts = parts / args.length
            loss = parts[0] + 0.5 * parts[1] + 0.2 * parts[2] + 0.5 * parts[3]
            parts[4] = back.detach()

            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            sched.step()
            step += 1
            sums += parts.detach().cpu().numpy()
            count += 1
            if step % 100 == 0:
                line = 'step %d  ' % step + '  '.join('%s %.4f' % (n, v) for n, v in zip(names, sums / count)) \
                    + '  %.2f s/step' % ((time.time() - start) / count)
                print(line, flush=True)
                log.write(line + '\n')
                log.flush()
                start, sums, count = time.time(), np.zeros(5), 0
            if step % 2000 == 0 or step == args.steps:
                torch.save({'model': model.state_dict(), 'opt': opt.state_dict(), 'sched': sched.state_dict(), 'step': step},
                           os.path.join(args.out, 'last.pt'))
            if step >= args.steps:
                break
    torch.save({'model': model.state_dict(), 'step': step}, os.path.join(args.out, 'denoiser.pt'))


if __name__ == '__main__':
    main()
