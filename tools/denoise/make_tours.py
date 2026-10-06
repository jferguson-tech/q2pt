# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Camera tours through Quake 2 maps, for the game's tour_file (game/g_tour.c).

Reads maps out of the game's .pak files (your own copy: nothing of them is
kept), finds places a player could stand by taking where the map's own
entities are, joins those that can see each other, and writes a walk between
them as short stretches ("clips") with a jump to somewhere new in between.

    python make_tours.py --baseq2 ../../run/baseq2 --out ../../run/baseq2/tours \
        --maps base1,base2 --clips 9 --clip-frames 8 --fps 30
"""
import argparse, math, os, random, struct


def read_paks(baseq2):
    """name -> (pak path, offset, length); later paks win, as in the game"""
    files = {}
    for n in range(10):
        path = os.path.join(baseq2, 'pak%d.pak' % n)
        if not os.path.exists(path):
            continue
        with open(path, 'rb') as f:
            magic, off, length = struct.unpack('<4sii', f.read(12))
            if magic != b'PACK':
                continue
            f.seek(off)
            for _ in range(length // 64):
                e = f.read(64)
                name = e[:56].split(b'\0')[0].decode('latin1').lower()
                pos, size = struct.unpack('<ii', e[56:])
                files[name] = (path, pos, size)
    return files


def read_file(files, name):
    path, pos, size = files[name]
    with open(path, 'rb') as f:
        f.seek(pos)
        return f.read(size)


class Bsp:
    def __init__(self, data):
        magic, version = struct.unpack('<4si', data[:8])
        assert magic == b'IBSP' and version == 38, 'not a Quake 2 map'
        lumps = [struct.unpack('<ii', data[8 + i * 8:16 + i * 8]) for i in range(19)]
        lump = lambda i: data[lumps[i][0]:lumps[i][0] + lumps[i][1]]
        self.entities = self.parse_entities(lump(0).split(b'\0')[0].decode('latin1'))
        p = lump(1)
        self.planes = [struct.unpack('<4f', p[i:i + 16]) for i in range(0, len(p), 20)]
        n = lump(4)
        self.nodes = [struct.unpack('<3i', n[i:i + 12]) for i in range(0, len(n), 28)]
        l = lump(8)
        self.leaf_contents = [struct.unpack('<i', l[i:i + 4])[0] for i in range(0, len(l), 28)]

    @staticmethod
    def parse_entities(text):
        out, cur, key = [], None, None
        i, n = 0, len(text)
        while i < n:
            c = text[i]
            if c == '{':
                cur = {}
            elif c == '}':
                if cur is not None:
                    out.append(cur)
                cur = None
            elif c == '"':
                j = text.index('"', i + 1)
                tok = text[i + 1:j]
                i = j
                if cur is not None:
                    if key is None:
                        key = tok
                    else:
                        cur[key] = tok
                        key = None
            i += 1
        return out

    def solid(self, p):
        node = 0
        while node >= 0:
            plane, front, back = self.nodes[node]
            a, b, c, d = self.planes[plane]
            node = front if a * p[0] + b * p[1] + c * p[2] - d >= 0 else back
        return (self.leaf_contents[-1 - node] & 1) != 0

    def roomy(self, p, r=14.0):
        """nothing solid within r of p, near enough"""
        if self.solid(p):
            return False
        for dx, dy, dz in ((r, 0, 0), (-r, 0, 0), (0, r, 0), (0, -r, 0), (0, 0, r), (0, 0, -r)):
            if self.solid((p[0] + dx, p[1] + dy, p[2] + dz)):
                return False
        return True

    def clear(self, a, b, step=10.0):
        d = [b[i] - a[i] for i in range(3)]
        length = math.sqrt(sum(x * x for x in d))
        n = max(1, int(length / step))
        for k in range(n + 1):
            t = k / n
            p = (a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t)
            if self.solid(p) or self.solid((p[0], p[1], p[2] - 10)) or self.solid((p[0], p[1], p[2] + 10)):
                return False
        return True


def places(bsp, rng):
    """eye positions: over the map's point entities, where there is room"""
    out = []
    for e in bsp.entities:
        cls = e.get('classname', '')
        if 'origin' not in e or cls == 'worldspawn':
            continue
        try:
            o = [float(v) for v in e['origin'].split()]
        except ValueError:
            continue
        if len(o) != 3:
            continue
        # lamps hang high and are no place to stand; things on the floor are
        lifts = (-40, -72, -110) if cls.startswith('light') else (30, 46, 60)
        for lift in lifts:
            p = (o[0], o[1], o[2] + lift)
            if bsp.roomy(p):
                out.append(p)
                break
    # no two on top of each other
    kept = []
    for p in out:
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2 > 48 ** 2 for q in kept):
            kept.append(p)
    return kept


def links(bsp, pts, reach=900.0, most=8):
    near = [[] for _ in pts]
    for i, p in enumerate(pts):
        cand = sorted((sum((p[k] - q[k]) ** 2 for k in range(3)), j) for j, q in enumerate(pts) if j != i)
        for d2, j in cand:
            if d2 > reach * reach or len(near[i]) >= most:
                break
            if d2 > 40 * 40 and bsp.clear(p, pts[j]):
                near[i].append(j)
    return near


def angles_to(a, b):
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = -math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return pitch, yaw


def turn(cur, want, most):
    d = (want - cur + 180.0) % 360.0 - 180.0
    return cur + max(-most, min(most, d))


def make_tour(bsp, rng, clips, clip_frames, fps, long_clips=False):
    """lines for one tour: clips of clip_frames film frames each"""
    pts = places(bsp, rng)
    near = links(bsp, pts)
    starts = [i for i in range(len(pts)) if near[i]]
    if not starts:
        return None
    hz = 10.0                                    # the game's own frames
    per_clip = int(math.ceil(clip_frames * hz / fps))
    lines = []
    for clip in range(1, clips + 1):
        at = rng.choice(starts)
        pos = list(pts[at])
        goal = rng.choice(near[at])
        speed = rng.choice([0.0, 120.0, 200.0, 300.0, 300.0, 400.0])      # units a second; 0 stands and looks
        pitch, yaw = angles_to(pos, pts[goal])
        yaw += rng.uniform(-40, 40)
        pitch = max(-40.0, min(40.0, pitch + rng.uniform(-15, 15)))
        look = None                              # a place being looked at instead of ahead
        turn_rate = rng.choice([20.0, 60.0, 120.0, 240.0]) / hz
        spin = rng.uniform(-90, 90) / hz if speed == 0.0 else 0.0
        # a shot some time from just before the clip to its end, so that some
        # clips open with one in flight or going off
        fire_at = rng.randrange(-4, per_clip) if rng.random() < 0.5 else -99
        fire_kind = rng.choice([1, 1, 2, 3, 4, 5, 5, 6])
        # a few frames to arrive in, not filmed, then the clip
        steps = [0] * 6 + [clip] * per_clip
        for n, mark in enumerate(steps):
            if mark:
                goal_p = pts[goal]
                d = [goal_p[k] - pos[k] for k in range(3)]
                dist = math.sqrt(sum(x * x for x in d))
                step = speed / hz
                if dist <= step or dist < 1.0:
                    pos = list(goal_p)
                    at = goal
                    options = near[at]
                    goal = rng.choice(options) if options else at
                    if rng.random() < 0.3 and options:
                        look = pts[rng.choice(options)]
                elif step > 0:
                    pos = [pos[k] + d[k] / dist * step for k in range(3)]
                target = look if look is not None else pts[goal]
                if spin:
                    yaw += spin
                elif (target[0] - pos[0]) ** 2 + (target[1] - pos[1]) ** 2 > 16:
                    want_pitch, want_yaw = angles_to(pos, target)
                    yaw = turn(yaw, want_yaw, turn_rate)
                    pitch += max(-turn_rate, min(turn_rate, max(-40.0, min(40.0, want_pitch)) - pitch))
            k = n - 6
            fire = 0
            if k == fire_at or (fire_kind == 5 and fire_at <= k < fire_at + 4):
                fire = fire_kind
            lines.append('%.2f %.2f %.2f %.2f %.2f %d %d' % (pos[0], pos[1], pos[2], pitch, yaw % 360.0, fire, mark))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--baseq2', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--maps', default='', help='comma separated; all maps in the paks if not given')
    ap.add_argument('--clips', type=int, default=9)
    ap.add_argument('--clip-frames', type=int, default=8)
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--tag', default='', help='added to the file names')
    args = ap.parse_args()

    files = read_paks(args.baseq2)
    maps = [m for m in args.maps.split(',') if m] or sorted(n[5:-4] for n in files if n.startswith('maps/') and n.endswith('.bsp'))
    os.makedirs(args.out, exist_ok=True)
    for m in maps:
        bsp = Bsp(read_file(files, 'maps/%s.bsp' % m))
        rng = random.Random('%s/%d' % (m, args.seed))
        lines = make_tour(bsp, rng, args.clips, args.clip_frames, args.fps)
        if not lines:
            print('%s: nowhere to go' % m)
            continue
        path = os.path.join(args.out, '%s%s.txt' % (m, args.tag))
        with open(path, 'w') as f:
            f.write('# %s: %d clips of %d frames at %g a second\n' % (m, args.clips, args.clip_frames, args.fps))
            f.write('\n'.join(lines) + '\n')
        print('%s: %d places, %d lines -> %s' % (m, len(places(bsp, rng)), len(lines), path))


if __name__ == '__main__':
    main()
