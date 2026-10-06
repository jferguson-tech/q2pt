# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Renders the frames the denoiser is trained and tested on.

For every map a camera tour is written (make_tours.py) and the game is run
on it with the RTX renderer, saving the buffers of each clip
(pt_render_export). Maps in HELD_OUT are never trained on: they are rendered
as the test set, at more paths, each tour once sharp and once with motion
blur. A job that finished is not done again, so this can be stopped and
started.

    python render_dataset.py --game ../../run --out /data/q2dn --split train
"""
import argparse, os, random, shutil, subprocess, sys, time

import make_tours

HELD_OUT = ['ware2', 'jail3', 'mine3', 'power2', 'city2', 'q2dm4']

# tag, blur, fog, clips, frames a clip, frames a second, paths a pixel in the reference
TRAIN_JOBS = [('a', 0.0, 1, 4, 12, 30, 512), ('b', 0.0, 0, 1, 12, 60, 512), ('c', 0.5, 1, 2, 12, 30, 512)]
# The test references have to be far cleaner than anything being compared
# with them: at 2048 paths they were the limit of what could be measured.
TEST_JOBS = [('s', 0.0, 1, 1, 12, 30, 16384), ('m', 0.5, 1, 1, 12, 30, 4096)]


def run_job(args, m, tag, blur, fog, clips, frames, fps, paths, tour_tag, seed):
    name = '%s_%s' % (m, tag)
    out = os.path.join(args.out, args.split, name)
    if os.path.exists(os.path.join(out, 'done')):
        return True
    shutil.rmtree(out, ignore_errors=True)

    baseq2 = os.path.join(args.game, 'baseq2')
    files = make_tours.read_paks(baseq2)
    bsp = make_tours.Bsp(make_tours.read_file(files, 'maps/%s.bsp' % m))
    lines = make_tours.make_tour(bsp, random.Random('%s/%s/%d' % (m, tour_tag, seed)), clips, frames, fps)
    if not lines:
        print('%s: nowhere to go' % name, flush=True)
        return False
    for c in range(1, clips + 1):
        os.makedirs(os.path.join(out, 'c%d' % c))
    tour = os.path.join(out, 'tour.txt')
    with open(tour, 'w') as f:
        f.write('\n'.join(lines) + '\n')

    with open(os.path.join(baseq2, 'q2dn_job.cfg'), 'w') as f:
        f.write('set fixedtime %d\n' % round(1000.0 / fps))
        f.write('set in_ignore 1\n')            # a key pressed in the window must not stop the run
        f.write('set pt_render_export 1\nset pt_render_blur %g\nset pt_render_fog %d\n' % (blur, fog))
        f.write('set pt_render_bounces 6\nset pt_render_light_samples 16\n')
        f.write('set tour_file "%s"\nset tour_notarget 0\n' % tour)
        f.write('set tour_clip_begin "set pt_offline_dir %s/c%%d; set pt_offline %d"\n' % (out, paths))
        f.write('set tour_clip_end "set pt_offline 0"\nset tour_end "quit"\n')

    start = time.time()
    log = open(os.path.join(out, 'log.txt'), 'w')
    try:
        code = subprocess.call(['./quake2', '+set', 'vid_ref', 'ptrtx', '+set', 'vid_fullscreen', '0',
                                '+set', 'gl_mode', str(args.mode), '+set', 's_initsound', '0',
                                '+exec', 'q2dn_job.cfg', '+map', m],
                               cwd=args.game, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        code = -1
    made = sum(len([x for x in os.listdir(os.path.join(out, 'c%d' % c)) if x.endswith('.ptx')]) for c in range(1, clips + 1))
    print('%s: %d frames in %.0f s (exit %d)' % (name, made, time.time() - start, code), flush=True)
    if code == 0 and made >= clips * frames:
        open(os.path.join(out, 'done'), 'w').close()
        return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True, help='the folder quake2 runs from')
    ap.add_argument('--out', required=True)
    ap.add_argument('--split', choices=['train', 'test'], required=True)
    ap.add_argument('--paths', type=int, default=0, help="paths a pixel in the reference, in place of each job's own")
    ap.add_argument('--mode', type=int, default=10, help='gl_mode: 10 is 1280x720')
    ap.add_argument('--maps', default='')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--timeout', type=int, default=7200)
    args = ap.parse_args()
    args.game = os.path.abspath(args.game)
    args.out = os.path.abspath(args.out)

    files = make_tours.read_paks(os.path.join(args.game, 'baseq2'))
    maps = sorted(n[5:-4] for n in files if n.startswith('maps/') and n.endswith('.bsp'))
    if args.split == 'train':
        maps = [m for m in maps if m not in HELD_OUT]
        jobs = TRAIN_JOBS
    else:
        maps = [m for m in maps if m in HELD_OUT]
        jobs = TEST_JOBS
    if args.maps:
        maps = [m for m in maps if m in args.maps.split(',')]

    # the game saves its settings when it leaves: put the player's own back after
    config = os.path.join(args.game, 'baseq2', 'config.cfg')
    backup = config + '.q2dn'
    if os.path.exists(config) and not os.path.exists(backup):
        shutil.copy(config, backup)
    try:
        for tag, blur, fog, clips, frames, fps, paths in jobs:
            for m in maps:
                # a test tour is the same sharp and blurred, to compare them
                tour_tag = 'test' if args.split == 'test' else tag
                run_job(args, m, tag, blur, fog, clips, frames, fps, args.paths or paths, tour_tag, args.seed)
    finally:
        if os.path.exists(backup):
            shutil.move(backup, config)


if __name__ == '__main__':
    main()
