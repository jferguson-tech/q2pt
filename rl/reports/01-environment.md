# Report 1: the environment

2026-10-10. Branch `rl-agent`. Machine as in report 0: Ryzen 9 7950X3D, one
RTX 4090 (not used here), Ubuntu 24.04, gcc 13.3.

## What there is now

* `q2ded`: the server alone, built from `linux/linux.cmake` with a C compiler
  and CMake only. SDL2, X11 and OpenGL are now optional; where they are
  missing, `q2ded` and the game library are all that is built. The full
  `quake2` and both path tracers still build here.
* `sv_singleplayer 1`: a dedicated server keeps `deathmatch 0` and runs the
  one-player game. Coop is not used.
* Stepping (`server/sv_rl.c`, two calls beside `SV_Frame` in
  `qcommon/common.c`): one request, one server frame of 100 ms, no clock.
* The player: client 0 of the one-player game, moved by `ClientThink` with
  one 100 ms command per frame (`game/g_rl.c`).
* What the player perceives (`game/g_rl_obs.c`): 32 numbers of its own
  state, 24 x 9 rays over a 90 x 60 degree view, the 16 nearest things in
  view with a clear line to the eye.
* `rl/q2env`: the Gymnasium environment, and a vector of them with one server
  process each.
* Demos written by the server, and `rl/tools/dm2check.py` to check their
  structure.

The teacher's action in the block is a placeholder (stand still) and the
guide and the progress reward are zero until the navigation is written.

## Determinism: passes

`rl/tests/test_determinism.py`, 4 tests, 1.0 s. For base1, base2 and bunk1,
600 steps of random actions with seed 123:

* repeated in the same process: identical;
* in a new process: identical;
* after another seed and another map were played in the process: identical;
* with seed 124: different, as it must be;
* with a demo being recorded: identical to without.

Identical means equal at every step in a hash of every entity's state and in
the bytes of everything the player perceives. Tested on this machine and
this build only; nothing says two different compilers or processors agree.

`rl/tests/test_layout.py` compiles `game/g_rl.h` and compares every offset
and constant with `rl/q2env/layout.py`: passes.

## Steps per second

Random actions on base1, forward 70% of the time, episodes of at most 1,000
steps, resets included (an episode lasted about 560 steps before the monsters
ended it). Python drives the servers from one unpinned process.

One server alone, 8 s each:

| thread | kind of core | steps/s |
| --- | --- | --- |
| 2 | large cache | 6,175 |
| 3 | large cache | 6,274 |
| 10 | faster clock | 6,884 |
| 11 | faster clock | 6,875 |

The faster-clocked cores step the game about 10% faster. The map and the
game fit in either cache.

Many servers together, 8 s each:

| servers | threads | total steps/s | each |
| --- | --- | --- | --- |
| 8 | 0-7 (large cache, one per core) | 29,906 | 3,738 |
| 8 | 8-15 (faster clock, one per core) | 32,779 | 4,097 |
| 16 | 0-15 (one per core) | 46,982 | 2,936 |
| 16 | 0-7, 16-23 (large cache, both threads) | 37,426 | 2,339 |
| 16 | 8-15, 24-31 (faster clock, both threads) | 41,235 | 2,577 |
| 28 | 2-15, 18-31 | 52,521 | 1,876 |
| 32 | 0-31 | 54,019 | 1,688 |

The total is above the 10,000 asked for in every row.

The rate per server falls from 6,900 alone to 4,100 with eight because the
one Python process that sets them going and copies their observations is the
limit, not the servers: about 30 microseconds of Python per server per step.
It has not been made faster, because a policy's forward pass will cost more
than that.

**Pinning chosen**: 28 servers on threads 2-15 and 18-31, faster-clocked
cores first when fewer are wanted. Cores 0 and 1 (threads 0, 1, 16, 17) are
left for the two Docker containers, the system and the training process.
The containers were idle when measured (264 MiB and 6 MiB).

A reset takes 1.6 ms once the map has been loaded in the process (7 ms the
first time), with the game library shut down and loaded again each time.

## Demos

Three episodes of random play on base1 were recorded (138, 1,200 and 1,200
frames; 29 kB to 123 kB).

* `dm2check.py` accounts for every byte of each, and of a demo recorded by a
  real client (`wtfact2.dm2`), which it reads the same way.
* No frame was too large for a demo block (0 dropped of 2,538).
* One was played in the full client on this machine through `pt_render` with
  the RTX renderer: 120 frames at 10 a second and 180 frames at 30 a second.
  The map loads, the view moves and turns, the gun, its shots and their
  sparks show, the status bar counts health and names a pickup, and the
  client reports no parse error. Looked at as still frames only; nobody has
  watched it as a film and the sound was not listened to.

## Not tested

* Windows: the new files are written to compile there (the transport is
  compiled out, the hooks are empty) but were not built. CI has not run:
  nothing is pushed.
* A machine without SDL2 and X11: the server-only path in the CMake file was
  not exercised, since this machine has both.
* Maps other than base1, base2 and bunk1.
* An episode that ends at the exit: random play does not get there.

## Installed

`uv` 0.13.0 in `~/.local/bin` (one program, no root), since the brief asks
for a uv environment and there was none; Python had neither `venv` nor `pip`.
In `~/q2pt-rl/venv`: numpy 2.5.3, gymnasium, zstandard. No system package.

## Disk

`~/q2pt-rl/`: 0.3 MB of demos and the Python environment (under 0.1 GB) of
the 500 GB allowed.

## Rebase

Main has not moved since the branch was made (8eb4fa7).
