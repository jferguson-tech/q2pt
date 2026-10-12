# rl: a scripted bot that teaches a neural policy to play Quake 2

Nobody plays the game here. A scripted bot, the teacher, plays a
single-player map with no display, reading the game's state and the map. A
neural policy, the student, learns to copy it from what a player could see,
and is then improved by reinforcement learning. Any run by either can be
saved as an ordinary `.dm2` demo, which the game plays and `pt_render`
renders.

This folder is the Python side: the environment, the training and the
tools. It is MIT licensed (`LICENSE`) and is never linked into the game; it
talks to a server process over shared memory. The game's side is GPL like
the code around it: `game/g_rl*.c`, `server/sv_rl.c`, `linux/sys_ded.c`.
Datasets, weights, demos and logs live in `~/q2pt-rl/`, never here.

No pretrained model, no code from any other bot, no reinforcement learning
framework: behaviour cloning, DAgger and PPO are written here in PyTorch.

## What it does, in numbers

Skill 1 (normal), single-player rules, on one machine (Ryzen 9 7950X3D, one
RTX 4090). The game's first unit is played a **leg** at a time: an episode
starts at the spawn point the exit of the map before leads to, with what the
teacher had when it left that map, and succeeds only at the exit that leads
on. Leaving by any other exit is a failure. (`reports/08-unit.md`.)

> **The tables below cannot be compared with those of reports 3 to 7**,
> nor with earlier versions of this page. Those were for a map played alone
> from its plain start with the blaster, with any exit counting; on most
> maps that is no test, since the player starts beside the door it would
> have come in by. The base1-only tables of reports 4 to 6 in particular
> (a student that beat its teacher there, PPO from random weights, a
> student not told the way) are for another task and another teacher, and
> those students were not retrained.

**The teacher**, 200 seeded episodes a leg:

| leg | reaches the exit that leads on | takes |
| --- | --- | --- |
| base1, from a new game | 196 of 200 | 101 s |
| base2, entered from base1 | 192 of 200 | 85 s |
| base3, entered from base2 | 168 of 200 | 226 s |
| base2, entered from base3 | 167 of 200 | 90 s |

Played as one run, each leg from exactly what the run carried out of the
last, it gets through the whole unit in 74 of 100 (`tools/unit.py`).

@@STUDENTS@@

The students are *guided*: at every step they are given the direction and
distance of the next point on the scripted planner's route. What they learn
is moving, aiming, shooting, dodging and picking things up, not finding the
way.

**The first unit of the game only.** A player that walks at random never
reaches the exit that leads on from any of these four starts; from two of
them it walks back out of the door behind it within 6 seconds
(`tools/walker.py`, which now tries every map from every place the game
brings the player into it). The legs of the later units have not been
worked out. mintro, which the teacher plays alone (report 7), is left out
of training for now: no student gets over its jumps.

## What is scripted and what is learned

Scripted, in the game library, written from the map format and the engine's
own functions:

* **A navigation graph** of each map, grown from where the player starts by
  moving a ghost of the player through the engine's own `Pmove`: walking,
  jumping, crouching, swimming, ladders, with doors and lifts tried at both
  ends of their travel. No recorded play. base1: 6,800 nodes, built in
  under a second and kept on disk.
* **A planner** that starts from the exit and works back through whatever
  opens the way: buttons to touch or shoot, triggers, keys, monsters whose
  death opens a door.
* **What hurts**: a link through a laser beam is shut while the beam is on,
  and the planner looks for what switches it; lava, slime and triggers that
  hurt are kept out of the graph.
* **Trial steps**: before the feet are told anything, the step is tried
  with the engine's own `Pmove` from where the player is and as it moves.
  At 100 ms a step is 30 units and stopping takes 17 more, and a ledge is
  overrun without this. A jump or a drop is begun only when a trial of the
  whole move lands where it should.
* **Combat**: the nearest monster a shot can reach, the aim taken a step
  ahead, the weapon that does most at the distance, a dodge to a
  neighbouring node; a monster that would take too long with the weapon in
  hand is run past.
* The teacher answers after every step with what it would do from where the
  player now is, whoever moved the player. It keeps no plan from one step to
  the next. That is what lets it label the student's own states for DAgger.

Learned: a recurrent network of 596,000 weights (`q2rl/model.py`). It sees
the player's own state, the ground a stride from its feet eight ways
round, a 9 x 24 grid of rays over a 90 x 60 degree view (distance, slope,
kind of thing hit), up to 16 things in view with a clear line to the eye,
its last action, and the guide. No position through a wall
and no map coordinate. It gives one choice in each of seven branches:
forward/back, strafe, jump/crouch, 15 yaw steps, 11 pitch steps, fire, and
which of ten weapons.

## How the game is driven

* `q2ded` is the server alone, built with a C compiler and CMake only:
  `cmake -S . -B build/linux && cmake --build build/linux --target q2ded game`.
* A dedicated server normally forces deathmatch. `sv_singleplayer 1` lets
  it run the one-player game, with the monsters and items of single player.
* The player is the one client of that game, with no network connection,
  moved only by the game's own `ClientThink`: no teleporting, no noclip.
* **One 100 ms move per server frame.** A real client sends many short
  moves; at 100 ms `Pmove` behaves a little differently, and the same step
  is used everywhere: building the graph, the teacher, the student.
* One request, one server frame, no wall clock. One process per
  environment. One server alone runs 6,900 steps a second with random
  actions; 28 together run 5,500 with the teacher playing, since the
  teacher now tries its steps, and reached 52,000 with random actions
  before the teacher existed.
* The same map, seed and actions give the same run, bit for bit:
  `python -m unittest discover -s tests` (7 tests, 5 s).

## Using it

```
uv venv ~/q2pt-rl/venv && uv pip install --python ~/q2pt-rl/venv/bin/python -r requirements.txt
cd rl

# the teacher on the legs of the base unit, in order; --keep saves what it carries out of each
python tools/play.py --episodes 200 --keep base1 base2@base1 base3@base2a base2@base3b
python tools/unit.py                             # the unit as one run, 100 times
python tools/play.py --table --episodes 28       # every map played alone, one line each
python tools/walker.py                           # where a player with no sense ends up, from every way in
python tools/explore.py                          # how much of each map the graph and explorer cover

# one whole run for one training seed: clone, DAgger, PPO, and every evaluation
python tools/run_seed.py --seed 0 --tag u1 --maps base2@base1 base3@base2a base2@base3b --held base1
python tools/table.py --tag u1                   # the runs so far as a table, mean ± spread over seeds

# or a stage at a time
python -m q2rl.bc --name bc --maps base3 mintro --episodes 900 --seed 0
python -m q2rl.dagger --start bc --name dagger --maps base3 mintro --teacher-episodes 900 --seed 0
python -m q2rl.ppo --name ppo --start dagger --maps base3 mintro
python -m q2rl.evaluate ~/q2pt-rl/weights/ppo.pt --maps base1 --episodes 100          # likeliest action
python -m q2rl.evaluate ~/q2pt-rl/weights/ppo.pt --maps base1 --episodes 100 --sample # actions drawn
python tools/record.py ~/q2pt-rl/weights/ppo.pt out.dm2 --map base3 --seed 500000 --greedy
python tools/dm2check.py out.dm2                 # is the demo well formed?
```

On this machine one run of `run_seed.py` takes about 75 minutes: 4 to
record 900 teacher episodes, 1.5 to clone, 25 for eight rounds of DAgger,
30 for PPO, the rest evaluating. Its data is about 6 GB.

In Python the environment follows the Gymnasium API:

```python
from q2env import Q2Env
env = Q2Env(map="base1")
obs, info = env.reset(seed=0)
obs, reward, terminated, truncated, info = env.step(env.action_space.sample())
info["teacher"]     # what the teacher would do from here
```

## Demos

A demo is written by the server as a recording client would have written
it: one block per server frame, holding the player's own view of the frame.
To play one, copy it into `baseq2/demos/` and type `demomap name.dm2`; to
render it, `pt_render name`. In `~/q2pt-rl/demos/unit_base/`: one teacher
run through the whole base unit, a file a leg@@DEMOS@@. Older demos of maps
played alone are in `~/q2pt-rl/demos/`. The first base1 demos were rendered
whole in the full client and came out at the length recorded; the newer
ones have passed `dm2check.py` only.

## What does not work

* **Everything after the first unit.** The legs of the other units have
  not been worked out, and most need what the teacher lacks: a way out by
  train, teleporter or a wall blown up, which the graph does not have, and
  a fight plan for tanks and bosses.
* **A map is not remembered between visits.** The game saves each map of a
  unit and loads it when the player comes back. Here base2 entered from
  base3 is base2 as new, its monsters alive again.
* **Maps with one door** (train, ware1, jail5, fact2, fact3, lab, space,
  mine4) are entered and left the same way, for something done inside.
  Scoring by the exit cannot test them and no leg is made of them.
* **The teacher runs out of time in 22 of 800 episodes** on the base unit,
  mostly standing where the graph has no way out of: a pit, the top of a
  lift.
* **The students start each leg with the teacher's inventory**, not with
  what they would have carried there themselves.
@@STUDENT_FAILS@@
* **No map with a laser across the way is finished.** Beams are handled
  (fact1 played alone: deaths by laser from 27 of 28 to about none), but
  fact1 is lost to its gunners.
* **mintro** (report 7): the teacher finishes it alone in 56 of 100, and no
  student gets over its first jump. Left out of training for now.
* **Not redone with the present teacher**: PPO from random weights, and the
  student that is not told the way (reports 5 and 6, base1 alone).
* **Narrow places at 100 ms steps.** Head-on into a wall `Pmove` does not
  slide; a corner overlapped by an eighth of a unit stops the player dead;
  a step cannot be climbed under a low roof. The teacher has a rule for
  each, and tries every step before taking it.
* **The player is always lit.** A client tells the server how much light
  falls on the player, and monsters far off overlook a player in the dark.
  A server with no renderer has no such reading.
* Demos have been looked at as rendered stills and measured for length.
  Nobody has watched one as a film or listened to it.

The reports in `reports/` have the measurements each of these statements
rests on, with the settings, and `PLAN.md` says how each part fits the
engine.
