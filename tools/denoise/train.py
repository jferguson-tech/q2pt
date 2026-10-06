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
from model import Denoiser, display, exposure_for, picture, warp

KEYS = ('light', 'variance', 'exact', 'albedo', 'specular', 'normal', 'depth', 'motion', 'known')


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
        scales = typical = None
        for f in opened:
            sets = rng.sample(range(f.sets), paths // f.set_paths)
            a = ptx.inputs(f, sets, box)
            a['ref_light'], a['ref_picture'] = ptx.reference(f, box)
            if scales is None:
                # brightness is judged on the whole first frame, as it is when a film is denoised
                step = 8
                acc = sum(ptx.clean(f.data[t * ptx.SET:t * ptx.SET + 9, ::step, ::step]) for t in sets) / len(sets)
                scales = ptx.scales(acc, 1)
                typical = ptx.typical(ptx.clean(f.data[ptx.ALL_PICTURE:ptx.ALL_PICTURE + 3, ::step, ::step]))
            if flip:
                a = {k: np.ascontiguousarray(v[:, :, ::-1]) for k, v in a.items()}
                a['normal'][0] *= -1
                a['motion'][0] *= -1
            for k in out:
                out[k].append(a[k])
        item = {k: torch.from_numpy(np.stack(v)) for k, v in out.items()}
        # a short clip is filled out by standing on its last frame
        if n < self.length:
            pad = self.length - n
            for k in item:
                item[k] = torch.cat([item[k], item[k][-1:].expand(pad, -1, -1, -1)])
            item['motion'][n:] = 0
            item['known'][n:] = 1
        item['paths'] = torch.tensor(paths)
        item['scales'] = torch.from_numpy(scales)
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
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    device = torch.device('cuda')
    torch.backends.cudnn.benchmark = True
    model = Denoiser().to(device).to(memory_format=torch.channels_last)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=args.lr, total_steps=args.steps, pct_start=0.03)
    step = 0
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
    names = ('picture', 'parts', 'shown', 'change')
    start, sums, count = time.time(), np.zeros(4), 0
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
            scales = item['scales'] / jitter[:, None]
            typical = item['typical'] * jitter
            expo = torch.tensor([exposure_for(t) for t in typical.tolist()], device=device).view(-1, 1, 1, 1)

            state, last_out, last_ref = None, None, None
            parts = torch.zeros(4, device=device)
            for t in range(args.length):
                if t and random.random() < 0.05:
                    state = None                             # a cut: learn to start again
                f = {k: item[k][:, t] for k in KEYS}
                with torch.autocast('cuda', dtype=torch.bfloat16):
                    light, state = model(f, item['paths'], scales, state)
                out = picture(light, f['albedo'], f['specular'], f['exact'])
                ref, ref_light = item['ref_picture'][:, t], item['ref_light'][:, t]
                parts[0] += relative(out * expo, ref * expo)
                # each part against its own reference, as it enters the picture
                parts[1] += (relative(f['albedo'] * light[:, 0:3] * expo, f['albedo'] * ref_light[:, 0:3] * expo)
                             + relative(f['specular'] * light[:, 3:6] * expo, f['specular'] * ref_light[:, 3:6] * expo)
                             + relative(light[:, 6:9] * expo, ref_light[:, 6:9] * expo)) / 3
                shown, want = display(out, expo, True), display(ref, expo, True)
                gx, gy = gradients(shown)
                rx, ry = gradients(want)
                parts[2] += F.l1_loss(shown, want) + 0.5 * (F.l1_loss(gx, rx) + F.l1_loss(gy, ry))
                if last_out is not None:
                    # how the picture changes from frame to frame should be how the reference changes
                    moved, inside = warp(torch.cat([last_out, last_ref], dim=1), f['motion'])
                    ok = inside * f['known']
                    parts[3] += (((shown - moved[:, 0:3]) - (want - moved[:, 3:6])).abs() * ok).mean()
                last_out, last_ref = shown, want
            parts = parts / args.length
            loss = parts[0] + 0.5 * parts[1] + 0.2 * parts[2] + 0.5 * parts[3]

            opt.zero_grad(set_to_none=True)
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
                start, sums, count = time.time(), np.zeros(4), 0
            if step % 2000 == 0 or step == args.steps:
                torch.save({'model': model.state_dict(), 'opt': opt.state_dict(), 'sched': sched.state_dict(), 'step': step},
                           os.path.join(args.out, 'last.pt'))
            if step >= args.steps:
                break
    torch.save({'model': model.state_dict(), 'step': step}, os.path.join(args.out, 'denoiser.pt'))


if __name__ == '__main__':
    main()
