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
# HELD_OUT has been measured after every training run and has steered the
# work, so it is no longer a blind test. These three are: from the two
# mission packs (two of The Reckoning's maps, one of Ground Zero's), drawn
# with random.Random('q2pt denoiser final test maps') on 2026-10-08 before
# any frame of either pack had been rendered. They are for --split final,
# once, when the work is finished: never train on them, and do not render
# or measure them before then. They need the packs' pak0.pak beside the
# game's own.
FINAL = ['xdm2', 'xmoon2', 'rdm7']

# tag, blur, fog, clips, frames a clip, frames a second, paths a pixel in the reference
TRAIN_JOBS = [('a', 0.0, 1, 4, 12, 30, 512), ('b', 0.0, 0, 1, 12, 60, 512), ('c', 0.5, 1, 2, 12, 30, 512)]
# a second helping, with more shooting and the BFG's flash more often: the first had few of either
MORE = dict(fire_chance=0.85, fire_kinds=(1, 2, 3, 4, 5, 6, 6, 6))
TRAIN_JOBS += [('d', 0.0, 1, 4, 12, 30, 512, MORE), ('e', 0.5, 1, 2, 12, 30, 512, MORE)]
# The test references have to be far cleaner than anything being compared
# with them: at 2048 paths they were the limit of what could be measured.
# Dark places are rare on a tour picked blindly, and are where a few paths
# a pixel leave the most noise. --dark N tries N clips a map small and
# cheaply, and renders in full those the game's exposure would have to
# brighten at least DARK times: at most DARK_MOST a map, the darkest first.
DARK, DARK_MOST = 2.0, 6
DARK_JOB = ('f', 0.0, 1, 12, 30, 512, dict(fire_chance=0.7, fire_kinds=(1, 2, 3, 4, 5, 6, 6)))
TYPICAL_TARGET = 0.0054      # pt/cpu/pt_cpu.cpp
# --dim renders every map with its own lights and sky turned down, so that
# every map gives dark clips. What is fired is as bright as ever: a flash in
# a dim room is the hard case. Job g turns them down 4 to 8 times (a
# strength drawn for each map), which leaves a bright map bright; job h by
# whatever makes the game's exposure brighten the map 4 to 16 times, going
# by the clips of job a already rendered of it.
DIM_JOBS = [('g', 0.0, 1, 1, 8, 30, 512, MORE), ('h', 0.0, 1, 1, 8, 30, 512, MORE)]

TEST_JOBS = [('s', 0.0, 1, 1, 12, 30, 16384), ('m', 0.5, 1, 1, 12, 30, 4096)]


def run_job(args, m, tag, blur, fog, clips, frames, fps, paths, tour_tag, seed, more, keep=None, mode=None, dim=1.0):
    """keep: of the tour's clips, the ones to render (numbered from 1), in that order"""
    name = '%s_%s' % (m, tag)
    out = os.path.join(args.out, args.split, name)
    if os.path.exists(os.path.join(out, 'done')):
        return True
    shutil.rmtree(out, ignore_errors=True)

    baseq2 = os.path.join(args.game, 'baseq2')
    files = make_tours.read_paks(baseq2)
    bsp = make_tours.Bsp(make_tours.read_file(files, 'maps/%s.bsp' % m))
    lines = make_tours.make_tour(bsp, random.Random('%s/%s/%d' % (m, tour_tag, seed)), clips, frames, fps, **more)
    if not lines:
        print('%s: nowhere to go' % name, flush=True)
        return False
    if keep is not None:
        each = len(lines) // clips               # a clip's lines: those to arrive in, then its own
        chosen = []
        for new, old in enumerate(keep, 1):
            for line in lines[(old - 1) * each:old * each]:
                head, mark = line.rsplit(' ', 1)
                chosen.append('%s %d' % (head, new if int(mark) else 0))
        lines, clips = chosen, len(keep)
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
        # the game keeps these from one run to the next: always said, so a dim job does not dim the one after
        f.write('set pt_surface_light %g\nset pt_point_light %g\nset pt_sky %g\n' % (1.0 / dim, 1.0 / dim, 2.0 / dim))
        f.write('set tour_file "%s"\nset tour_notarget 0\n' % tour)
        f.write('set tour_clip_begin "set pt_offline_dir %s/c%%d; set pt_offline %d"\n' % (out, paths))
        f.write('set tour_clip_end "set pt_offline 0"\nset tour_end "quit"\n')

    start = time.time()
    log = open(os.path.join(out, 'log.txt'), 'w')
    try:
        code = subprocess.call(['./quake2', '+set', 'vid_ref', args.renderer, '+set', 'vid_fullscreen', '0',
                                '+set', 'gl_mode', str(args.mode if mode is None else mode), '+set', 's_initsound', '0',
                                '+exec', 'q2dn_job.cfg', '+map', m],
                               cwd=args.game, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        code = -1
    made = sum(len([x for x in os.listdir(os.path.join(out, 'c%d' % c)) if x.endswith('.ptx')]) for c in range(1, clips + 1))
    print('%s: %d frames in %.0f s (exit %d)' % (name, made, time.time() - start, code), flush=True)
    # at 60 frames a second a clip now and then comes out a frame short; it is still a clip
    if code == 0 and made >= clips * (frames - 1):
        open(os.path.join(out, 'done'), 'w').close()
        return True
    return False


def dark_clips(args, m, tries):
    """the clips of a map's tour that are dark enough, found by rendering all of them small"""
    import numpy as np
    import ptx
    tag, blur, fog, frames, fps, paths, more = DARK_JOB
    found = os.path.join(args.out, args.split, '%s_%s.dark' % (m, tag))
    if os.path.exists(found):
        return [int(x) for x in open(found).read().split()]
    probe = os.path.join(args.out, args.split, '%s_probe' % m)
    keep = []
    if run_job(args, m, 'probe', blur, fog, tries, frames, fps, 16, tag, args.seed, more, mode=0):
        dark = []
        for c in range(1, tries + 1):
            typical, lit = [], []
            for x in sorted(os.listdir(os.path.join(probe, 'c%d' % c))):
                if x.endswith('.ptx'):
                    pic = ptx.clean(ptx.Frame(os.path.join(probe, 'c%d' % c, x)).data[ptx.ALL_PICTURE:ptx.ALL_PICTURE + 3, ::2, ::2])
                    typical.append(ptx.typical(pic))
                    lit.append(float((pic.sum(axis=0) > 1e-5).mean()))
            # the middle one: a shot's flash in a dark room leaves it a dark room. A tour that
            # starts inside a wall is black, not dark: most of the picture must have light in it
            if typical and TYPICAL_TARGET / float(np.median(typical)) >= DARK and float(np.median(lit)) >= 0.5:
                dark.append((float(np.median(typical)), c))
        keep = [c for _, c in sorted(dark)[:DARK_MOST]]
        with open(found, 'w') as f:
            f.write(' '.join(str(c) for c in keep) + '\n')
    shutil.rmtree(probe, ignore_errors=True)
    return keep


def dim_for(args, m, tag):
    """how many times a map's lights are turned down for a dim job"""
    rng = random.Random('dim/%s/%s/%d' % (m, tag, args.seed))
    if tag == 'g':
        return 2.0 ** rng.uniform(2.0, 3.0)
    import glob
    import numpy as np
    import ptx
    first = sorted(glob.glob(os.path.join(args.out, args.split, '%s_a' % m, 'c*', '*.ptx')))[::6]
    if not first:
        return 0.0
    typical = float(np.median([ptx.typical(ptx.clean(ptx.Frame(p).data[ptx.ALL_PICTURE:ptx.ALL_PICTURE + 3, ::8, ::8])) for p in first]))
    return max(1.0, 2.0 ** rng.uniform(2.0, 4.0) * typical / TYPICAL_TARGET)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True, help='the folder quake2 runs from')
    ap.add_argument('--out', required=True)
    ap.add_argument('--split', choices=['train', 'test', 'final'], required=True)
    ap.add_argument('--paths', type=int, default=0, help="paths a pixel in the reference, in place of each job's own")
    ap.add_argument('--mode', type=int, default=10, help='gl_mode: 10 is 1280x720')
    ap.add_argument('--maps', default='')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--timeout', type=int, default=7200)
    ap.add_argument('--renderer', default='ptrtx', help='ptrtx or ptcpu')
    ap.add_argument('--dim', action='store_true', help="instead of the usual jobs: every map with its lights turned down")
    ap.add_argument('--dark', type=int, default=0, help='instead of the usual jobs: try this many clips a map and render the dark ones')
    args = ap.parse_args()
    args.game = os.path.abspath(args.game)
    args.out = os.path.abspath(args.out)

    files = make_tours.read_paks(os.path.join(args.game, 'baseq2'))
    maps = sorted(n[5:-4] for n in files if n.startswith('maps/') and n.endswith('.bsp'))
    if args.split == 'train':
        maps = [m for m in maps if m not in HELD_OUT + FINAL]
        jobs = TRAIN_JOBS
    elif args.split == 'final':
        maps = [m for m in maps if m in FINAL]
        jobs = TEST_JOBS
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
        if args.dark:
            tag, blur, fog, frames, fps, paths, more = DARK_JOB
            for m in maps:
                keep = dark_clips(args, m, args.dark)
                print('%s: %d dark of %d' % (m, len(keep), args.dark), flush=True)
                if keep:
                    run_job(args, m, tag, blur, fog, args.dark, frames, fps, args.paths or paths, tag, args.seed, more, keep=keep)
            jobs = []
        elif args.dim:
            jobs = DIM_JOBS
        for tag, blur, fog, clips, frames, fps, paths, *more in jobs:
            for m in maps:
                # a test tour is the same sharp and blurred, to compare them
                tour_tag = 'test' if args.split != 'train' else tag
                dim = dim_for(args, m, tag) if args.dim else 1.0
                if not dim:
                    print('%s_%s: no clips of job a to go by' % (m, tag), flush=True)
                    continue
                if args.dim:
                    print('%s_%s: lights turned down %.1f times' % (m, tag, dim), flush=True)
                run_job(args, m, tag, blur, fog, clips, frames, fps, args.paths or paths, tour_tag, args.seed, more[0] if more else {}, dim=dim)
    finally:
        if os.path.exists(backup):
            shutil.move(backup, config)


if __name__ == '__main__':
    main()
