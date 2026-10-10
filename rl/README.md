# rl: a scripted bot that teaches a neural policy to play Quake 2

Nobody plays the game here. A scripted bot, the teacher, plays a
single-player map with no display, reading the game's state and the map. A
neural policy, the student, learns to copy it from what a player could see,
and is then improved by reinforcement learning until, on the one map this
has been done for, it reaches the exit more often than its teacher. Any run
by either can be saved as an ordinary `.dm2` demo, which the game plays and
`pt_render` renders.

This folder is the Python side: the environment, the training and the
tools. It is MIT licensed (`LICENSE`) and is never linked into the game; it
talks to a server process over shared memory. The game's side is GPL like
the code around it: `game/g_rl*.c`, `server/sv_rl.c`, `linux/sys_ded.c`.
Datasets, weights, demos and logs live in `~/q2pt-rl/`, never here.

No pretrained model, no code from any other bot, no reinforcement learning
framework: behaviour cloning, DAgger and PPO are written here in PyTorch.

## What it does, in numbers

Everything below is base1, the game's first map, at skill 1 (normal) with
single-player rules, on one machine (Ryzen 9 7950X3D, one RTX 4090). An
episode succeeds when the map's exit is reached. 200 evaluation episodes on
seeds no training run sees:

| player | what it is | reaches the exit |
| --- | --- | --- |
| teacher | scripted | 182 of 200, 91% |
| cloned student | learned from 600 teacher episodes | 145, 72% |
| DAgger student | 8 rounds of driving while the teacher labels | 165, 82% |
| PPO from the DAgger student | 3.0 M steps of reinforcement learning | 195, 98% |
| PPO from random weights | the same reward and 3.0 M steps | 0 of 56 |
| unguided DAgger student | not given the route; 6 rounds | 118, 59% |

The students are *guided*: at every step they are given the direction and
distance of the next point on the scripted planner's route. What they learn
is moving, aiming, shooting, dodging and picking things up, not finding the
way. The fine-tuned student samples its actions; playing its most likely
action it reaches the exit in 86%. The others play their most likely action.

**One map.** Of the game's 39 single-player maps the teacher plays one
through. So there is one training map, and nothing here shows a student
playing a map it was not trained on. One run of each thing, one seed.

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
* **Combat**: the nearest monster a shot can reach, aim with lead, the
  weapon by distance, a dodge to a neighbouring node, away from barrels.
* The teacher answers after every step with what it would do from where the
  player now is, whoever moved the player. It keeps no plan from one step to
  the next. That is what lets it label the student's own states for DAgger.

Learned: a recurrent network of 596,000 weights (`q2rl/model.py`). It sees
the player's own state, a 9 x 24 grid of rays over a 90 x 60 degree view
(distance, slope, kind of thing hit), up to 16 things in view with a clear
line to the eye, its last action, and the guide. No position through a wall
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
  actions and 3,700 with the teacher playing; 28 together reached 52,000
  with random actions before the teacher existed.
* The same map, seed and actions give the same run, bit for bit:
  `python -m unittest discover -s tests` (7 tests, 5 s).

## Using it

```
uv venv ~/q2pt-rl/venv && uv pip install --python ~/q2pt-rl/venv/bin/python -r requirements.txt
cd rl

python tools/play.py base1                       # the teacher, 100 episodes, failures classed
python tools/explore.py                          # how much of each map the graph and explorer cover
python -m q2rl.bc --name bc                      # record the teacher, clone it, evaluate
python -m q2rl.dagger --start bc --name dagger   # the student drives, the teacher labels
python -m q2rl.ppo --name ppo_ft --start dagger  # PPO from the imitation weights
python -m q2rl.ppo --name ppo_scratch            # PPO from random weights
python -m q2rl.evaluate ~/q2pt-rl/weights/ppo_ft.pt --episodes 200 --sample
python tools/curves.py ppo_ft ppo_scratch        # the learning curves, one pair of axes
python tools/record.py ~/q2pt-rl/weights/ppo_ft.pt out.dm2 --seed 500000
python tools/dm2check.py out.dm2                 # is the demo well formed?
```

On this machine: recording 600 teacher episodes takes 77 s, cloning 3.5
minutes, eight rounds of DAgger 25 minutes, each PPO run 19 minutes. The
data is 3.9 GB.

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
render it, `pt_render name`. Three recorded on this machine, in
`~/q2pt-rl/demos/`: the teacher through base1 (144.6 s), the fine-tuned
student through base1 (86.9 s), and the explorer walking base1 with the
monsters taken out (180 s). Each was rendered whole in the full client and
came out at the length it was recorded.

## What does not work

* **38 of 39 maps.** The teacher finishes base1 in 96 of 100 episodes. On
  27 other maps it reaches a forward exit in 3 of 28 or fewer: it meets
  gunners, tanks and bosses with only the blaster, walks into lava and
  lasers, or has no plan because the way hangs on a train, a teleporter, a
  wall to be blown up or a key that lies in another map. On 9 maps it
  "passes" only because the exit that leads on is at the start or near it.
* **Playing a unit across its maps**, inventory kept, which is what the
  game's hubs need: not done.
* **The unguided student**, which is not told the way, reaches base1's exit
  in 59% after DAgger (33% cloned), against 82% guided. It was trained and
  tested on the one map, so that is a route learned by heart, not a way
  found. It was not fine-tuned with PPO.
* **From random weights PPO learns nothing** in 3 million steps but to stay
  alive by standing still.
* **Narrow places at 100 ms steps.** Head-on into a wall `Pmove` does not
  slide; a corner overlapped by an eighth of a unit stops the player dead;
  a step cannot be climbed under a low roof. The teacher has a rule for
  each. With monsters out, its explorer reaches 92% of the goals it sets
  itself across all 39 maps; the rest it gives up on.
* **The player is always lit.** A client tells the server how much light
  falls on the player, and monsters far off overlook a player in the dark.
  A server with no renderer has no such reading.
* **Windows.** The game-side files are written to compile there, with the
  stepping compiled out, and have not been built there.
* Demos have been looked at as rendered stills and measured for length.
  Nobody has watched one as a film or listened to it.

The reports in `reports/` have the measurements each of these statements
rests on, with the settings, and `PLAN.md` says how each part fits the
engine.
