# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Measures the denoiser on the test maps (render_dataset.py --split test).

For every clip: the noisy frames, Intel Open Image Denoise frame by frame
(if --oidn is given), and this denoiser at 4, 8 and 16 paths a pixel, each
against the reference. Also the two ways of getting a motion blurred film:
denoising blurred frames, or denoising sharp ones and blurring afterwards.

Numbers are on the picture as shown (exposed, filmic curve, gamma):
  PSNR     higher is closer to the reference
  SSIM     likewise, on structure
  flicker  how much the change from one frame to the next differs from the
           reference's change, times 1000: lower is steadier

    python evaluate.py --data /data/q2dn/test --weights denoiser.pt --out report
"""
import argparse, glob, os, subprocess, tempfile

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

import ptx
from model import display, exposure_for, warp
from pt_denoise import Film, load_model, motion_blur


def write_pfm(path, a):
    a = np.ascontiguousarray(np.transpose(a, (1, 2, 0))[::-1], dtype='<f4')
    with open(path, 'wb') as f:
        f.write(b'PF\n%d %d\n-1.0\n' % (a.shape[1], a.shape[0]))
        f.write(a.tobytes())


def read_pfm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        a = np.frombuffer(f.read(), dtype='<f4' if scale < 0 else '>f4').reshape(h, w, 3)
    return np.transpose(a[::-1], (2, 0, 1)).copy()


def oidn(binary, light, albedo, normal):
    with tempfile.TemporaryDirectory() as d:
        write_pfm(d + '/c.pfm', light)
        write_pfm(d + '/a.pfm', np.clip(albedo, 0, 1))
        write_pfm(d + '/n.pfm', normal)
        subprocess.run([binary, '--hdr', d + '/c.pfm', '--alb', d + '/a.pfm', '--nrm', d + '/n.pfm',
                        '--clean_aux', '-o', d + '/o.pfm'], check=True, stdout=subprocess.DEVNULL)
        return read_pfm(d + '/o.pfm')


def ssim(a, b):
    """a, b [3,H,W] in 0-1"""
    k = torch.arange(11, dtype=torch.float32, device=a.device) - 5
    g = torch.exp(-k * k / (2 * 1.5 * 1.5))
    g = (g / g.sum())[None, None]
    blur = lambda x: F.conv2d(F.conv2d(x[:, None], g[..., None]), g[:, :, None])[:, 0]
    ma, mb = blur(a), blur(b)
    va, vb, cov = blur(a * a) - ma * ma, blur(b * b) - mb * mb, blur(a * b) - ma * mb
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    return float((((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())


class Score:
    def __init__(self):
        self.mse, self.ssim, self.flicker, self.n, self.nf = 0.0, 0.0, 0.0, 0, 0
        self.last = None

    def add(self, shown, want, motion, known):
        self.mse += float(((shown - want) ** 2).mean())
        self.ssim += ssim(shown, want)
        self.n += 1
        if self.last is not None and float(known.mean()) > 0.5:
            moved, inside = warp(torch.cat(self.last)[None], motion[None])
            ok = inside[0] * known
            d = ((shown - moved[0, 0:3]) - (want - moved[0, 3:6])).abs() * ok
            self.flicker += float(d.sum() / (ok.sum() * 3 + 1))
            self.nf += 1
        self.last = (shown, want)

    def row(self):
        return '%.2f | %.4f | %.2f' % (-10 * np.log10(self.mse / self.n), self.ssim / self.n,
                                       1000 * self.flicker / max(self.nf, 1))


def save_strip(path, pictures):
    row = torch.cat([p.clamp(0, 1) for p in pictures], dim=2)
    Image.fromarray((row * 255 + 0.5).byte().permute(1, 2, 0).cpu().numpy()).save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', required=True)
    ap.add_argument('--weights', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--oidn', default='', help='path to oidnDenoise')
    ap.add_argument('--films', action='store_true', help='also save every frame of noisy | denoised | reference, for making videos')
    args = ap.parse_args()

    device = torch.device('cuda')
    model = load_model(args.weights, device)
    os.makedirs(args.out, exist_ok=True)
    names = ['noisy 4', 'noisy 16', 'ours 4', 'ours 8', 'ours 16'] + (['OIDN 16'] if args.oidn else [])
    total = {kind: {n: Score() for n in names} for kind in ('sharp', 'blurred')}
    blur_ways = {n: Score() for n in ('blurred frames denoised', 'sharp frames denoised, then blurred', 'sharp reference, then blurred')}
    lines = []
    by_frame = {}        # (name, frames into the clip) -> [sum of squared error, count]: does it gain as a film goes on?

    sharp_dirs = sorted(glob.glob(os.path.join(args.data, '*_s')))
    for sd in sharp_dirs:
        name = os.path.basename(sd)[:-2]
        md = sd[:-2] + '_m'
        for clip in sorted(os.listdir(sd)):
            if not clip.startswith('c') or not os.path.isdir(os.path.join(sd, clip)):
                continue
            for kind, d in (('sharp', sd), ('blurred', md)):
                paths = sorted(glob.glob(os.path.join(d, clip, '*.ptx')))
                if not paths:
                    continue
                frames = [ptx.Frame(p) for p in paths]
                expo = exposure_for(ptx.typical(ptx.reference(frames[0])[:, ::8, ::8]))
                films = {4: Film(model, device, 4), 8: Film(model, device, 8), 16: Film(model, device, 16)}
                scores = {n: Score() for n in names}
                post = Score(), Score()
                for i, f in enumerate(frames):
                    ref = torch.from_numpy(ptx.reference(f)).to(device)
                    want = display(ref, expo)
                    out = {}
                    for n, film in films.items():
                        light, motion, known = film.step(f)
                        out['ours %d' % n] = light
                    for n in (4, 16):
                        out['noisy %d' % n] = torch.from_numpy(ptx.features(f, list(range(n // 4)))[0]).to(device)
                    if args.oidn:
                        l, a, nrm = ptx.features(f, [0, 1, 2, 3])[:3]
                        out['OIDN 16'] = torch.from_numpy(oidn(args.oidn, l, a, nrm)).to(device)
                    for n in names:
                        shown = display(out[n], expo)
                        scores[n].add(shown, want, motion, known)
                        total[kind][n].add(shown, want, motion, known)
                        if kind == 'sharp':
                            e = by_frame.setdefault((n, i), [0.0, 0])
                            e[0] += float(((shown - want) ** 2).mean())
                            e[1] += 1
                    # one clip may follow another in total: do not compare across the join
                    if i == len(frames) - 1:
                        for n in names:
                            total[kind][n].last = None

                    if kind == 'blurred':
                        blur_ways['blurred frames denoised'].add(display(out['ours 16'], expo), want, motion, known)
                    else:
                        # the same moment of the same tour, rendered blurred: what blurring afterwards is up against
                        mp = os.path.join(md, clip, os.path.basename(f.path))
                        if os.path.exists(mp):
                            mf = ptx.Frame(mp)
                            mwant = display(torch.from_numpy(ptx.reference(mf)).to(device), expo)
                            mm, mk = (torch.from_numpy(a).to(device) for a in ptx.features(mf, [0])[4:6])
                            share = mf.blur if mf.blurred else 0.5
                            for key, src in (('sharp frames denoised, then blurred', out['ours 16']), ('sharp reference, then blurred', ref)):
                                blurred = motion_blur(src, motion * known, share)
                                blur_ways[key].add(display(blurred, expo), mwant, mm, mk)
                            if i == len(frames) // 2:
                                save_strip(os.path.join(args.out, '%s_%s_blurways.png' % (name, clip)),
                                           [display(motion_blur(out['ours 16'], motion * known, share), expo), mwant])

                    if i == len(frames) // 2:
                        order = ['noisy 4', 'ours 4', 'noisy 16'] + (['OIDN 16'] if args.oidn else []) + ['ours 16']
                        save_strip(os.path.join(args.out, '%s_%s_%s.png' % (name, clip, kind)),
                                   [display(out[n], expo) for n in order] + [want])
                    if args.films:
                        fd = os.path.join(args.out, 'film_%s_%s_%s' % (name, clip, kind))
                        os.makedirs(fd, exist_ok=True)
                        save_strip(os.path.join(fd, '%04d.png' % i),
                                   [display(out['noisy 16'], expo), display(out['ours 16'], expo), want])
                for key in blur_ways:
                    blur_ways[key].last = None
                for n in names:
                    lines.append('| %s %s %s | %s | %s |' % (name, clip, kind, n, scores[n].row()))
                print('%s %s %s: ours 16 %s' % (name, clip, kind, scores['ours 16'].row()), flush=True)

    with open(os.path.join(args.out, 'results.md'), 'w') as f:
        for kind in ('sharp', 'blurred'):
            f.write('## All test clips, %s\n\n| | PSNR | SSIM | flicker |\n|---|---|---|---|\n' % kind)
            for n in names:
                if total[kind][n].n:
                    f.write('| %s | %s |\n' % (n, total[kind][n].row()))
            f.write('\n')
        f.write('## Motion blur: two ways, against frames rendered blurred (16 paths)\n\n| | PSNR | SSIM | flicker |\n|---|---|---|---|\n')
        for key, s in blur_ways.items():
            if s.n:
                f.write('| %s | %s |\n' % (key, s.row()))
        f.write('\n## PSNR by how far into a clip the frame is (sharp)\n\n| frame | ' + ' | '.join(names) + ' |\n|---|' + '---|' * len(names) + '\n')
        for i in sorted({k[1] for k in by_frame}):
            f.write('| %d | ' % (i + 1) + ' | '.join('%.2f' % (-10 * np.log10(by_frame[(n, i)][0] / by_frame[(n, i)][1])) for n in names) + ' |\n')
        f.write('\n## Each clip\n\n| clip | | PSNR | SSIM | flicker |\n|---|---|---|---|---|\n' + '\n'.join(lines) + '\n')
    print(open(os.path.join(args.out, 'results.md')).read().split('## Each clip')[0])


if __name__ == '__main__':
    main()
