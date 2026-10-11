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

Skill 1 (normal), single-player rules, each map played alone from a fresh
start with the blaster, on one machine (Ryzen 9 7950X3D, one RTX 4090). An
episode succeeds when the map's exit is reached.

**The teacher** plays three maps through. 100 seeded episodes each:

| map | reaches the exit | takes |
| --- | --- | --- |
| base1, the first map | 98 of 100 | 105 s |
| base3: water, a key, parasites | 86 of 100 | 89 s |
| mintro: lava, ledges, berserkers | 56 of 100 | 212 s |

**The students**, trained on base3 and mintro and never shown base1. Three
whole runs from different seeds, 100 evaluation episodes a map each, on
seeds no training uses. Mean of the three runs ± the spread between them,
playing the likeliest action; in brackets, drawing actions instead:

| player | base3 | mintro | base1, held out |
| --- | --- | --- | --- |
| teacher | 83% | 43% | 99% |
| cloned from 900 teacher episodes | 26% ± 3 (12% ± 5) | 0% (0%) | 0% ± 0 (38% ± 20) |
| DAgger, 8 rounds | 46% ± 8 (29% ± 6) | 0% (0%) | 19% ± 33 (55% ± 8) |
| PPO from the DAgger student, 3.0 M steps | 72% ± 11 (54% ± 11) | 0% (0%) | 31% ± 44 (66% ± 18) |

* On base3 each stage adds and PPO comes near the teacher.
* **No student finishes mintro.** They die in lava at the first jump
  between ledges, a move the teacher makes only after trying it out.
* On base1, which they never saw, the three runs score 0, 81 and 12 of 100
  with the likeliest action: two of them stop at one crawl-space. Drawing
  actions gets through it.

The students are *guided*: at every step they are given the direction and
distance of the next point on the scripted planner's route. What they learn
is moving, aiming, shooting, dodging and picking things up, not finding the
way.

**Three maps, not thirty-nine.** Of the game's 39 single-player maps only
13 are a test when played alone: on the others a player that walks at
random reaches an exit, because in a hub the player arrives beside the way
back (`tools/walker.py`). The teacher finishes 3 of the 13. An earlier
set of figures for base1 alone (a student that beat its teacher there,
PPO from random weights learning nothing, a student not told the way) is in
reports 4 to 6; the teacher and the observation have changed since and
those students were not retrained.

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

python tools/play.py base1 base3 mintro          # the teacher, 100 episodes a map, failures classed
python tools/play.py --table --episodes 28       # every map, one line each
python tools/walker.py                           # which maps a player with no sense also "finishes"
python tools/explore.py                          # how much of each map the graph and explorer cover

# one whole run for one training seed: clone, DAgger, PPO, and every evaluation
python tools/run_seed.py --seed 0 --tag m2 --maps base3 mintro --held base1
python tools/table.py --tag m2                   # the runs so far as a table, mean ± spread over seeds

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
render it, `pt_render name`. In `~/q2pt-rl/demos/`: the teacher through
base1, base3 and mintro, a PPO student through base3, and a PPO student
through base1, which it never trained on. The first base1 demos were
rendered whole in the full client and came out at the length recorded; the
newer ones have passed `dm2check.py` only.

## What does not work

* **36 of 39 maps.** Thirteen maps are a test when played alone, and the
  teacher finishes three. On the other ten it has no plan (the way out
  needs a train, a teleporter or a wall blown up, which the graph does not
  have), or it meets gunners, tanks and bosses with the blaster.
* **No map with a laser across the way is finished.** Beams are handled
  (fact1: deaths by laser from 27 of 28 to about none), but fact1 is lost
  to its gunners.
* **mintro is beyond every student**, and base1 unseen is a matter of the
  seed with the likeliest action: see the table at the top.
* **The teacher on mintro** finishes 56 of 100: knocked into lava while
  fighting near an edge, or killed by three berserkers.
* **Playing a unit across its maps**, inventory kept, which is what the
  game's hubs need and what would make the other 26 maps tests: not done.
* **Not redone with the present teacher**: PPO from random weights, and the
  student that is not told the way (reports 5 and 6, base1 only).
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
