# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""Checks the structure of a Quake 2 demo (.dm2, protocol 34).

    python rl/tools/dm2check.py <file.dm2> [...]

A demo is a run of blocks, each a 32 bit length and that many bytes of server
messages, ended by a length of -1. Every block is read message by message the
way the client reads it, and every byte must be accounted for. Checked as
well: the header a recording client writes comes first (server data for a
demo, configstrings, baselines, the command to load the map); frame numbers
rise; every frame that is a delta names an earlier frame of this file no more
than 12 frames back, which is as far as the server will delta from; and
after the header nearly every block holds a frame. A server playing a demo
deals out one block per frame of its own, so a block with no frame in it
holds the picture still for a tenth of a second: more than one such block in
a hundred fails the check.

Prints one line of what the file holds, or what is wrong and where. Exits
with 1 if any file fails. This reads the structure only: that the picture is
right can only be seen by playing the demo.
"""

import struct
import sys

MAX_MSGLEN = 1400
PROTOCOL = 34
MAX_ITEMS = 256
DELTA_REACH = 12        # UPDATE_BACKUP - 3 - 1

(SVC_BAD, SVC_MUZZLEFLASH, SVC_MUZZLEFLASH2, SVC_TEMP_ENTITY, SVC_LAYOUT, SVC_INVENTORY,
 SVC_NOP, SVC_DISCONNECT, SVC_RECONNECT, SVC_SOUND, SVC_PRINT, SVC_STUFFTEXT,
 SVC_SERVERDATA, SVC_CONFIGSTRING, SVC_SPAWNBASELINE, SVC_CENTERPRINT, SVC_DOWNLOAD,
 SVC_PLAYERINFO, SVC_PACKETENTITIES, SVC_DELTAPACKETENTITIES, SVC_FRAME) = range(21)

# What follows the type byte of each temporary entity the base game sends:
# P a position (6 bytes), D a direction (1), B a byte, S a short.
TE = {
    0: "PD", 1: "PD", 2: "PD", 3: "PP", 4: "PD", 5: "P", 6: "P", 7: "P", 8: "P",
    9: "PD", 10: "BPDB", 11: "PP", 12: "PD", 13: "PD", 14: "PD", 15: "BPDB",
    16: "SPP", 17: "P", 18: "P", 19: "SPP", 20: "P", 21: "P", 22: "P", 23: "PP",
    24: "SPPP", 25: "BPDB", 26: "PD", 27: "PP", 28: "P", 29: "BPDB",
}
TE_SIZE = {"P": 6, "D": 1, "B": 1, "S": 2}


class Bad(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.d, self.i = data, 0

    def left(self):
        return len(self.d) - self.i

    def take(self, n):
        if self.i + n > len(self.d):
            raise Bad("ran off the end of the block")
        out = self.d[self.i:self.i + n]
        self.i += n
        return out

    def byte(self):
        return self.take(1)[0]

    def short(self):
        return struct.unpack("<h", self.take(2))[0]

    def long(self):
        return struct.unpack("<i", self.take(4))[0]

    def string(self):
        end = self.d.find(b"\0", self.i)
        if end < 0:
            raise Bad("a string with no end")
        out = self.d[self.i:end]
        self.i = end + 1
        return out


def entity_bits(r):
    bits = r.byte()
    if bits & 0x80:
        bits |= r.byte() << 8
    if bits & 0x8000:
        bits |= r.byte() << 16
    if bits & 0x800000:
        bits |= r.byte() << 24
    number = r.short() if bits & (1 << 8) else r.byte()
    return bits, number


def entity_fields(r, bits):
    n = 0
    for bit in (11, 20, 21, 22, 4):         # the four models, frame as a byte
        n += bool(bits & (1 << bit))
    n += 2 * bool(bits & (1 << 17))         # frame as a short
    for a, b in ((16, 25), (14, 19), (12, 18)):   # skin, effects, renderfx
        x, y = bits & (1 << a), bits & (1 << b)
        n += 4 if x and y else 1 if x else 2 if y else 0
    for bit in (0, 1, 9):                   # origin
        n += 2 * bool(bits & (1 << bit))
    for bit in (10, 2, 3):                  # angles
        n += bool(bits & (1 << bit))
    n += 6 * bool(bits & (1 << 24))         # old origin
    n += bool(bits & (1 << 26))             # sound
    n += bool(bits & (1 << 5))              # event
    n += 2 * bool(bits & (1 << 27))         # solid
    r.take(n)


def playerstate(r):
    flags = r.short() & 0xFFFF
    sizes = (1, 6, 6, 1, 1, 2, 6, 3, 6, 3, 4, 1, 1, 7, 1)   # in the order of the PS_ bits
    r.take(sum(s for bit, s in enumerate(sizes) if flags & (1 << bit)))
    stats = r.long() & 0xFFFFFFFF
    r.take(2 * bin(stats).count("1"))


def check(path):
    data = open(path, "rb").read()
    pos = 0
    blocks = 0
    count = {"frames": 0, "full": 0, "sounds": 0, "effects": 0, "prints": 0, "entities": 0}
    seen = {}               # frame number -> its place in the file's order
    last_frame = None
    header_done = False
    got = {"serverdata": False, "configstrings": 0, "baselines": 0, "precache": False}
    largest = 0
    ended = False
    empty = 0               # blocks after the header with no frame in them
    header_blocks = None
    frames_before = 0

    while pos < len(data):
        if pos + 4 > len(data):
            raise Bad(f"{len(data) - pos} stray bytes at the end")
        (length,) = struct.unpack_from("<i", data, pos)
        pos += 4
        if length == -1:
            ended = True
            if pos != len(data):
                raise Bad(f"{len(data) - pos} bytes after the end mark")
            break
        if not 0 < length <= MAX_MSGLEN:
            raise Bad(f"block {blocks} has length {length}")
        if pos + length > len(data):
            raise Bad(f"block {blocks} runs past the end of the file")
        r = Reader(data[pos:pos + length])
        pos += length
        largest = max(largest, length)

        try:
            while r.left():
                cmd = r.byte()
                if cmd == SVC_SERVERDATA:
                    if blocks or r.i != 1:
                        raise Bad("server data is not the first thing in the file")
                    if r.long() != PROTOCOL:
                        raise Bad("not protocol 34")
                    r.long()
                    if r.byte() != 1:
                        raise Bad("not marked as a demo for a client to play")
                    r.string()
                    if r.short() < 0:
                        raise Bad("no player number: this is a server demo with no view")
                    got["map"] = r.string().decode(errors="replace")
                    got["serverdata"] = True
                elif cmd == SVC_CONFIGSTRING:
                    r.short()
                    r.string()
                    if not header_done:
                        got["configstrings"] += 1
                elif cmd == SVC_SPAWNBASELINE:
                    if header_done:
                        raise Bad("a baseline after the header")
                    bits, _ = entity_bits(r)
                    entity_fields(r, bits)
                    got["baselines"] += 1
                elif cmd == SVC_STUFFTEXT:
                    if r.string() == b"precache\n" and not header_done:
                        got["precache"] = True
                        header_done = True
                elif cmd == SVC_FRAME:
                    if not header_done:
                        raise Bad("a frame before the header was complete")
                    number, delta = r.long(), r.long()
                    r.byte()
                    r.take(r.byte())
                    if last_frame is not None and number <= last_frame:
                        raise Bad(f"frame {number} follows frame {last_frame}")
                    if delta > 0:
                        if delta not in seen:
                            raise Bad(f"frame {number} is a delta from {delta}, which is not in the file")
                        if number - delta > DELTA_REACH:
                            raise Bad(f"frame {number} is a delta from {delta}, too far back")
                    else:
                        count["full"] += 1
                    if not count["frames"] and delta > 0:
                        raise Bad("the first frame is a delta")
                    if r.byte() != SVC_PLAYERINFO:
                        raise Bad(f"frame {number} has no player state")
                    playerstate(r)
                    if r.byte() != SVC_PACKETENTITIES:
                        raise Bad(f"frame {number} has no entities")
                    while True:
                        bits, ent = entity_bits(r)
                        if not ent:
                            break
                        if not bits & (1 << 6):     # not a removal
                            entity_fields(r, bits)
                        count["entities"] += 1
                    seen[number] = count["frames"]
                    last_frame = number
                    count["frames"] += 1
                elif cmd == SVC_SOUND:
                    flags = r.byte()
                    r.byte()
                    for bit, size in ((0, 1), (1, 1), (4, 1), (3, 2), (2, 6)):
                        if flags & (1 << bit):
                            r.take(size)
                    count["sounds"] += 1
                elif cmd in (SVC_MUZZLEFLASH, SVC_MUZZLEFLASH2):
                    r.take(3)
                    count["effects"] += 1
                elif cmd == SVC_TEMP_ENTITY:
                    kind = r.byte()
                    if kind not in TE:
                        raise Bad(f"temporary entity {kind} is not one the base game sends")
                    r.take(sum(TE_SIZE[c] for c in TE[kind]))
                    count["effects"] += 1
                elif cmd == SVC_PRINT:
                    r.byte()
                    r.string()
                    count["prints"] += 1
                elif cmd in (SVC_CENTERPRINT, SVC_LAYOUT):
                    r.string()
                    count["prints"] += 1
                elif cmd == SVC_INVENTORY:
                    r.take(2 * MAX_ITEMS)
                elif cmd == SVC_NOP:
                    pass
                else:
                    raise Bad(f"message {cmd} is not one a demo holds")
        except Bad as e:
            raise Bad(f"block {blocks}, byte {r.i}: {e}") from None
        blocks += 1
        if header_done and count["frames"] == frames_before and header_blocks is not None:
            empty += 1
        if header_done and header_blocks is None:
            header_blocks = blocks
        frames_before = count["frames"]

    if not ended:
        raise Bad("no end mark: the file was cut short")
    for part in ("serverdata", "precache"):
        if not got[part]:
            raise Bad(f"the header has no {part}")
    if not got["baselines"]:
        raise Bad("the header has no baselines")
    if not count["frames"]:
        raise Bad("no frames")
    if empty * 100 > count["frames"]:
        raise Bad(f"{empty} blocks after the header hold no frame: played back, the picture "
                  f"would stand still for {empty / 10:.0f} s in all")

    return (f"{got['map']!r}: {count['frames']} frames ({count['frames'] / 10:.1f} s, "
            f"{count['full']} not delta), {blocks} blocks, largest {largest} bytes, "
            f"{got['configstrings']} configstrings, {got['baselines']} baselines, "
            f"{count['entities']} entity updates, {count['sounds']} sounds, "
            f"{count['effects']} effects, {count['prints']} prints, {len(data)} bytes")


if __name__ == "__main__":
    failed = False
    for path in sys.argv[1:]:
        try:
            print(f"{path}: ok, {check(path)}")
        except Bad as e:
            print(f"{path}: BAD, {e}")
            failed = True
    sys.exit(1 if failed else 0)
