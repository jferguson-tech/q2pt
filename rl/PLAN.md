# rl: plan

A scripted bot (the teacher) plays the single-player maps with no display. A
neural policy (the student) learns to copy it and is then improved by
reinforcement learning. Any run can be saved as a `.dm2` demo that `pt_render`
renders. Nobody plays the game by hand at any point.

This file says how each part fits into this port of the engine, with the
files it touches. The reports in `rl/reports/` hold what was measured.

## Rules this work keeps to

* No pretrained models, no code from other bots, no RL framework: behaviour
  cloning, DAgger and PPO are written here in PyTorch.
* Python lives in `rl/` (MIT, see `rl/LICENSE`) and is a separate process. It
  is never linked into the game.
* Engine and game changes are GPL v2 like the code around them. New game code
  is in new files `game/g_rl*.c`; new server code is in `server/sv_rl.c`.
  `pt/`, `ref_pt/` and `neural/` are not touched.
* Datasets, weights, demos and logs go in `~/q2pt-rl/`, at most 500 GB.
  Nothing from the game's data goes into the repository.

## The step: one 100 ms move per server frame

The server runs at 10 frames a second (`FRAMETIME` 0.1). The player is moved
by exactly one `usercmd_t` with `msec = 100` in each server frame. A real
client sends many short moves per frame, and `Pmove` does not behave the same
at 100 ms: friction and acceleration are applied once, a jump covers its arc
in a few long steps, and a stair is climbed in one move or not at all.

So **100 ms is the step length everywhere**: when the navigation graph tests
whether one place can be reached from another, when the teacher plays and
when the student plays. Nothing in this project moves the player with any
other step.

## Engine side

### A server-only program: `q2ded`

`linux/linux.cmake` asks for SDL2 and X11 before anything else. It is
reordered so that two targets need only a C compiler and CMake:

* `q2ded`: `qcommon/*.c`, `server/*.c`, `null/cl_null.c`,
  `linux/net_udp.c`, `linux/q_shlinux.c`, `linux/glob.c`, `game/q_shared.c`
  and a new `linux/sys_ded.c`, built with `DEDICATED_ONLY` (which
  `qcommon/common.c` already understands).
* `game`, as now.

`linux/sys_ded.c` is `linux/sys_sdl.c` without SDL: the entry point, the game
library loader and console output. Its main loop does not wait for a
millisecond to pass when the stepping mode is on, which the loop in
`sys_sdl.c` does and which would hold the server to 1,000 frames a second.

SDL2, X11 and OpenGL become optional: where they are found, `quake2`, `ref_gl`
and the path tracers are built as before; where they are not, CMake says so
and builds `q2ded` and `game` only. The Windows `CMakeLists.txt` gains
`server/sv_rl.c` in `quake2`; the game library there already takes every file
in `game/`.

### Single-player rules on a dedicated server

`SV_InitGame` (`server/sv_init.c`) sets `deathmatch 1` on a dedicated server
unless `coop` is set. A new cvar `sv_singleplayer` (0 by default) makes that
one test skip, so the server goes down the existing branch for one player:
`maxclients 1`, `deathmatch 0`, `coop 0`. That is the game a player starts
from the menu: the same monsters, items and skill rules. Coop is not used.

What still differs from a game played in the full program, all of it outside
the play itself:

* no autosave is written on a map change (`SV_GameMap_f` already skips it on
  a dedicated server), and an episode never follows a map change;
* an episode starts through `SV_Map` directly, not the `map` command, so the
  `save/current` folder is not wiped or written: thirty processes share one
  game folder;
* the opening cinematic and the help computer are not shown.
* a client tells the server how much light falls where the player stands,
  and monsters far off do not notice a player in the dark. A server with no
  renderer has no such reading, so the player always counts as standing in
  plain light (`RL_LIGHT_LEVEL` in `game/g_rl.c`).

### The player: a fake client

The player is edict 1, the only client slot of a one-player game, driven
through the game's own `ClientThink` and nothing else: no teleporting, no
noclip, no setting of velocity.

* `server/sv_rl.c` fills `svs.clients[0]` the way `SV_DirectConnect` and
  `SV_Begin_f` do for a real client (`ge->ClientConnect`, `ge->ClientBegin`,
  state `cs_spawned`), but with no network address. The slot is needed so
  the server builds this client's frames and collects the messages meant for
  it, which is what a demo is made of.
* One test in existing code serves that slot: `SV_SendClientMessages`
  (`server/sv_send.c`) hands it to `SV_RL_SendClient` in place of the
  network, which also marks it as heard from, so it never times out.
* `game/g_rl.c` turns an action into a `usercmd_t` and calls `ClientThink`.
  Weapon changes go through the game's own `use` item code, as a key press
  would.

### Stepping

`Qcommon_Frame` (`qcommon/common.c`) calls two hooks around the server frame,
leaving the timed line as it is:

```
msec = SV_RL_BeginFrame (msec);
PERF_TIMED ("pt_perf_server", SV_Frame (msec));
SV_RL_EndFrame ();
```

Both return at once unless the program was started with `+set rl_shm <name>`.

* `SV_RL_BeginFrame` blocks until Python asks for something. For a step it
  hands the action to the game (`ge->ServerCommand` with `rl act`, the
  existing `sv` command path, so the game interface is not changed) and
  returns 100. `SV_Frame (100)` then runs exactly one game frame: the
  server's clock moves by the 100 ms it is given and never reads the wall
  clock.
* `SV_RL_EndFrame` asks the game for the observation, the reward terms and
  the teacher's action (`rl observe`), writes the demo frame if one is being
  recorded, and wakes Python.
* A reset request names a map and a seed: see below.

One process per environment. Data is in a shared memory block
(`/dev/shm/<name>`, mapped by the server, the game and Python) with a fixed
layout declared in `game/g_rl.h` and mirrored in `rl/q2env/layout.py`; a test
compares the two. Wake-ups go over a pair of pipes inherited from Python. The
server hands the block's address to the game in the cvar `rl_block`, so the
game library has no system calls of its own. The transport is Linux only and is
compiled out on Windows, where the hooks are empty.

### Determinism

Same map, seed and actions must give the same trajectory, bit for bit.

* A reset calls `srand (seed)` in `server/sv_rl.c` before `SV_Map`, and the
  game seeds again in its own library on `rl reset` (on Windows the game
  library can have a C runtime of its own). The game calls `rand ()` in about
  250 places and `SV_SpawnServer` and `SV_Frame` call it too; all of them
  follow from that one seed.
* A reset sets `sv.state = ss_dead` first, so `SV_Map` shuts the game library
  down and loads it again: its static variables start from zero each episode.
  If that costs too much it is replaced by clearing the state by hand, and
  the test below decides whether that is safe.
* The wall clock reaches the game in one place, `func_clock` with the time of
  day (`game/g_misc.c`); maps that use it are listed when found.
* `rl/tests/test_determinism.py`: the same seed and actions twice in one
  process, in two processes, and after other episodes have run in the
  process; a hash of every edict's state and the observation is compared at
  every step.

### How an episode ends

* **exit**: `BeginIntermission` has run (`level.intermissiontime` set). In
  stepping mode `ExitLevel` (`game/g_main.c`) does not issue `gamemap`, so the
  map change is never followed.
* **death**: the player's health is zero or less.
* **time**: a limit in steps, given at reset.

A map starts with its clock a second ahead of the server's. A reset levels
the two, so that the first step runs a game frame like every other.

Playing on through a unit, inventory kept, is the stretch goal: it needs the
level files in `save/current`, so each process would get a game folder of its
own.

### Demos

`serverrecord` is not usable: `SV_RecordDemoMessage` writes every entity in
the map and no player state, so there is no first-person view in it. Client
`record` needs a real client.

The route taken: the server writes what a recording client would have
written. `server/sv_rl.c`, on `rl_demo <file>` in a reset request:

1. the header `CL_Record_f` writes: `svc_serverdata` (protocol 34, attract
   loop 1, player number 0, the map's name), every configstring, every
   baseline from `sv.baselines`, then `stufftext "precache\n"`, split into
   blocks under `MAX_MSGLEN`;
2. each server frame, one block: the client's reliable messages (prints,
   configstring changes, the inventory), then `SV_BuildClientFrame` and
   `SV_WriteFrameToClient`, delta compressed against the last frame written,
   then the unreliable messages (sounds, muzzle flashes, temporary
   entities). If the block would pass `MAX_MSGLEN` the unreliable part is
   left out, as it is on a real connection; if the frame alone is too large
   it is skipped and the next one is a delta from the last one written;
3. a length of -1 at the end.

Demos are at the server's 10 frames a second, as every Quake 2 demo is; the
client interpolates between frames on playback.

Checks: `rl/tools/dm2check.py` parses a file block by block and command by
command and reports any byte it cannot account for. On this machine the full
`quake2` is built too, so each change to the writer is also played with
`demomap` and rendered with `pt_render`.

The other route (log the seed and usercmds, replay through a real client that
records) is not taken: a real client's moves are not 100 ms long, so the
replay would not be the same run.

## Teacher

All of it is in the game library (`game/g_rl_*.c`), written from the map
format and the engine's own functions.

* **Navigation graph** (`g_rl_nav.c`), built per map on first use and kept
  in `~/q2pt-rl/nav/`. It is grown from where the player starts: a ghost of
  the player is put at a node at rest and moved through `gi.Pmove` in 100 ms
  steps, forward on each of 16 headings, and where that is stopped or falls,
  with a jump and crouched; in water, rising, level and sinking; at a ladder,
  up it. Where the ghost comes to stand becomes a node and the move a link.
  A link is kept only if the move also works from 6 units behind the node
  and 4 to either side. Doors and lifts have two places, home and away: the
  graph is grown with all at home, then with each group of them away, and a
  link found near one is marked with the end it needs. A node on a lift has
  a twin at the lift's other end, joined by the ride. No recorded play is
  used. Buttons, keys, trains and walls that can be shot away are not
  handled yet.
* **Planner** (`g_rl_plan.c`): reads the spawned entities (doors, buttons,
  keys, triggers and their targets, lifts, the exit) and builds the chain of
  sub-goals that opens the way to `target_changelevel`. It is recomputed from
  the current world state, not remembered.
* **Combat** (`g_rl_fight.c`): target choice, aim with lead for projectile
  weapons, weapon choice by range and ammunition, strafing, retreat, pickups.
* **Statelessness**: the teacher's action is a function of the world as it is
  now (plus fixed per-map data), so it can label a state the student drove
  into. It is computed every step and returned with the observation whoever
  is acting.
* **Output**: an action in the student's own action space, with turn rate and
  turn acceleration limited so that the view looks like play.

## Student

**Observation**, only what a player could know:

* own status: health, armour, ammunition, the weapon held and those owned,
  velocity, view angles, on ground, in water;
* a fan of ray casts inside the field of view: for each, the distance and the
  kind of thing hit (world, liquid, door or lift, monster, item, sky);
* the entities in the field of view with a clear line to the eye, nearest
  first, up to a fixed number: kind, bearing, distance, and for monsters
  whether they are hurt or dead;
* guided variant only: bearing and distance of the planner's next waypoint.

No position seen through a wall and no map coordinates of goals. The number
of rays is set from what a step is measured to cost.

**Action**, multi-discrete: forward/back (3), strafe (3), jump/none/crouch
(3), yaw change (bins, finer near zero), pitch change (bins), fire (2),
weapon (keep, or one of the ten).

**Policy**: an encoder for each part of the observation, a GRU, one head per
action branch and a value head. `rl/q2rl/model.py`.

## Training

| step | file | what |
| --- | --- | --- |
| 1 | `rl/q2rl/bc.py` | behaviour cloning on teacher rollouts |
| 2 | `rl/q2rl/dagger.py` | the student drives a share of the steps, the teacher labels all of them, the data is added to and the student retrained |
| 3 | `rl/q2rl/ppo.py` | PPO from the imitation weights: value head first with the policy frozen, then both, with a penalty for leaving the teacher's action that falls to zero |
| 4 | `rl/q2rl/ppo.py` | PPO from random weights, same reward and budget |

Reward: progress along the planner's route, damage dealt, kills, the exit;
penalties for damage taken, death and time.

There is one graphics card. Evaluation runs between training phases, not
beside them. The environments run on the processor only.

## Layout

```
rl/
  PLAN.md  README.md  LICENSE  requirements.txt
  q2env/     the Gymnasium environment, the vector of processes, the layout
  q2rl/      model, cloning, DAgger, PPO, evaluation
  tools/     dm2check.py, benchmarks, plots
  tests/     determinism, layout, demo structure
  reports/   one per milestone
game/g_rl.h  g_rl.c  g_rl_obs.c  g_rl_nav.c  g_rl_plan.c  g_rl_fight.c
server/sv_rl.c
linux/sys_ded.c
```

Small edits to existing files: `qcommon/common.c` (two hook calls),
`qcommon/qcommon.h` or `server/server.h` (their declarations),
`server/sv_init.c` (`sv_singleplayer`), `server/sv_send.c` (the fake
client's slot), `game/g_main.c` (`ExitLevel`), `game/g_svcmds.c` (the `rl`
command), `game/g_combat.c` (damage counted for the reward),
`linux/linux.cmake`, `CMakeLists.txt`.

## The machine

Found on 2026-10-10:

| | |
| --- | --- |
| name, system | taco, Ubuntu 24.04.4, Linux 7.0.0-28 |
| processor | Ryzen 9 7950X3D, 16 cores, 32 threads. Cores 0-7 (threads 0-7 and 16-23) share a 96 MB L3 cache; cores 8-15 (threads 8-15 and 24-31) share 32 MB |
| memory | 125 GiB, 120 GiB available; 8 GiB swap |
| card | one RTX 4090, 24,564 MiB, driver 595.84 |
| disk | NVMe, 3.6 TB, 3.4 TB free |
| build tools | gcc and g++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, GNU Make 4.3: all present |
| client libraries | SDL2 2.30.0 and X11 1.8.7 headers present; the full `quake2` builds and there is a display to run it on |
| game data | `run/baseq2/pak0.pak`, `pak1.pak`, `pak2.pak`: 39 single-player maps and 8 deathmatch maps |
| Docker | two containers running, not ours. They are not restricted to any cores and were using 264 MiB and 6 MiB of memory. The Docker socket cannot be read by this user, so their names are not known |

Lacking:

* **uv**. Python 3.12.3 is installed but without `venv` or `pip`, so the
  virtual environment cannot be made with what is here. uv installs as one
  program in `~/.local/bin` without root and is not a system package.
* **glslc**: only needed for the RTX renderer's shaders; the existing build
  uses `glslangValidator` instead.

Both kinds of core report the same top frequency to the system, so which kind
steps the game faster is measured, not assumed. Four threads
are left free for the containers and the system.
