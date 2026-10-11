# Report 0: the plan and the machine

2026-10-10. Branch `rl-agent`, from main at 8eb4fa7. No code yet: this step
read the engine and wrote `rl/PLAN.md`.

## The machine, as found

| | |
| --- | --- |
| name, system | taco, Ubuntu 24.04.4, Linux 7.0.0-28 |
| processor | Ryzen 9 7950X3D, 16 cores, 32 threads; cores 0-7 have the 96 MB L3 cache, cores 8-15 have 32 MB |
| memory | 125 GiB, 120 GiB available |
| card | one RTX 4090, 24,564 MiB, 48 MiB in use, driver 595.84 |
| disk | 3.6 TB NVMe, 3.4 TB free |
| build tools | gcc 13.3.0, CMake 3.28.3, Ninja 1.11.1, Make 4.3 |
| client libraries | SDL2 2.30.0, X11 1.8.7, and a display |
| game data | present in `run/baseq2`: 39 single-player maps, 8 deathmatch maps |
| Docker | two containers, 264 MiB and 6 MiB of memory, not pinned to cores; names not readable by this user |
| data directory | `~/q2pt-rl/` does not exist yet: 0 GB of the 500 GB cap |

Missing: `uv`, and Python has neither `venv` nor `pip`. Nothing was installed.

## What reading the code settled

* **Single-player rules**: one test in `SV_InitGame` forces deathmatch on a
  dedicated server. Skipping it behind a cvar gives the one-player game with
  no other change, so coop is not needed.
* **Stepping**: `SV_Frame (100)` runs exactly one game frame and reads no
  clock. The only wall-clock wait is in the program's main loop, which the
  server-only program replaces.
* **Demos**: `serverrecord` holds no player state, as suspected. The server
  can write what a recording client writes, from the frames it already builds
  for each client, if the bot has a client slot on the server.
* **The fake client** therefore has a slot in `svs.clients` as well as its
  edict in the game. It is still moved only by `ClientThink`.
* **Seeding**: the server and the game share one `rand ()` on Linux; a reset
  seeds it before the map is spawned and reloads the game library so its
  static variables start clean.
* **Shared game folder**: a map change writes level files to `save/current`.
  Episodes end at the exit and never follow the change, so many processes can
  share `run/baseq2`.

## Not known yet

* Steps per second, and which kind of core is faster.
* What reloading the game library on each reset costs.
* Whether any demo frame passes the 1,400 byte block limit in practice.
* How well `Pmove` at 100 ms climbs stairs and makes jumps that the maps
  expect of a player moving in short steps.

## Rebase

`rl-agent` is on the latest main; nothing to rebase.
