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
from model import Denoiser, display, exposure_for, squash, warp


class Clips(torch.utils.data.Dataset):
    """Stretches of consecutive frames, cut to a square, at 4, 8 or 16 paths a pixel."""

    def __init__(self, root, length=8, size=256, epoch=4000, seed=0):
        self.clips = []
        for d in sorted(glob.glob(os.path.join(root, '*', 'c*'))):
            frames = sorted(glob.glob(os.path.join(d, '*.ptx')))
            if len(frames) >= 2:
                self.clips.append(frames)
        if not self.clips:
            raise SystemExit('no clips under %s' % root)
        self.length, self.size, self.epoch, self.seed = length, size, epoch, seed

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
        paths = rng.choice([4, 8, 16])
        flip = rng.random() < 0.5

        out = {k: [] for k in ('light', 'albedo', 'normal', 'depth', 'motion', 'known', 'ref')}
        typical = None
        for f in opened:
            sets = rng.sample(range(f.sets), paths // f.set_paths)
            if typical is None:
                typical = f.typical(sets)
            light, albedo, normal, depth, motion, known = ptx.features(f, sets, y0, y0 + s, x0, x0 + s)
            ref = ptx.reference(f, y0, y0 + s, x0, x0 + s)
            if flip:
                light, albedo, normal, depth, motion, known, ref = (
                    np.ascontiguousarray(a[:, :, ::-1]) for a in (light, albedo, normal, depth, motion, known, ref))
                normal[0] *= -1
                motion[0] *= -1
            for k, a in zip(out, (light, albedo, normal, depth, motion, known, ref)):
                out[k].append(a)
        item = {k: torch.from_numpy(np.stack(v)) for k, v in out.items()}
        # a short clip is filled out by standing on its last frame
        if n < self.length:
            pad = self.length - n
            for k in item:
                item[k] = torch.cat([item[k], item[k][-1:].expand(pad, -1, -1, -1)])
            item['motion'][n:] = 0
            item['known'][n:] = 1
        item['paths'] = torch.tensor(paths)
        item['typical'] = torch.tensor(typical)
        return item


def gradients(x):
    return x[..., :, 1:] - x[..., :, :-1], x[..., 1:, :] - x[..., :-1, :]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--steps', type=int, default=30000)
    ap.add_argument('--batch', type=int, default=6)
    ap.add_argument('--length', type=int, default=9)
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
                                         persistent_workers=True, prefetch_factor=4,
                                         worker_init_fn=lambda i: torch.manual_seed(torch.initial_seed() + i))
    log = open(os.path.join(args.out, 'log.txt'), 'a')
    start, sums, count = time.time(), np.zeros(4), 0
    while step < args.steps:
        for item in loader:
            item = {k: v.to(device, non_blocking=True) for k, v in item.items()}
            b = item['light'].shape[0]
            # brightness varies more between films than within this data
            jitter = torch.exp2(torch.empty(b, device=device).uniform_(-1.5, 1.5))
            light_scale = jitter[:, None, None, None, None]
            light, ref = item['light'] * light_scale, item['ref'] * light_scale
            typical = item['typical'] * jitter
            scale = 0.05 / typical                           # for the network
            expo = torch.tensor([exposure_for(t) for t in typical.tolist()], device=device).view(-1, 1, 1, 1)

            state, last_out, last_ref = None, None, None
            loss_parts = torch.zeros(4, device=device)
            for t in range(args.length):
                if t and random.random() < 0.05:
                    state = None                             # a cut: learn to start again
                with torch.autocast('cuda', dtype=torch.bfloat16):
                    out, state = model(light[:, t], item['albedo'][:, t], item['normal'][:, t], item['depth'][:, t],
                                       item['motion'][:, t], item['known'][:, t], item['paths'], scale, state)
                shown, want = display(out, expo, True), display(ref[:, t], expo, True)
                s = scale.view(-1, 1, 1, 1)
                loss_parts[0] += F.l1_loss(shown, want)
                loss_parts[1] += F.l1_loss(squash(out * s), squash(ref[:, t] * s))
                gx, gy = gradients(shown)
                rx, ry = gradients(want)
                loss_parts[2] += F.l1_loss(gx, rx) + F.l1_loss(gy, ry)
                if last_out is not None:
                    # how the picture changes from frame to frame should be how the reference changes
                    moved, inside = warp(torch.cat([last_out, last_ref], dim=1), item['motion'][:, t])
                    ok = inside * item['known'][:, t]
                    loss_parts[3] += (((shown - moved[:, 0:3]) - (want - moved[:, 3:6])).abs() * ok).mean()
                last_out, last_ref = shown, want
            loss_parts = loss_parts / args.length
            loss = loss_parts[0] + 0.5 * loss_parts[1] + 0.5 * loss_parts[2] + 1.0 * loss_parts[3]

            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            sched.step()
            step += 1
            sums += loss_parts.detach().cpu().numpy()
            count += 1
            if step % 100 == 0:
                line = 'step %d  shown %.4f  log %.4f  edges %.4f  change %.4f  %.2f s/step' % (
                    (step,) + tuple(sums / count) + ((time.time() - start) / count,))
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
