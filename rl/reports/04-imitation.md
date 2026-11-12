# Report 4: the guided student, by imitation

2026-10-10. Branch `rl-agent`. Machine as in report 0: one RTX 4090, 28
server processes on threads 2-15 and 18-31. Step length 100 ms, skill 1.

## Result

On base1, over the same 200 evaluation episodes (seeds the training never
sees, 6,000 steps allowed, the policy playing its most likely action):

| player | reached the exit | died | out of time | kills | time |
| --- | --- | --- | --- | --- | --- |
| teacher (scripted) | 182 of 200, 91% | 15 | 3 | 15.1 | 136 s |
| student, cloning only | 145 of 200, 72% | 36 | 19 | 14.1 | 167 s |
| student, DAgger, 8 rounds | 165 of 200, 82% | 32 | 3 | 14.5 | 141 s |
| student, DAgger, after round 5 | 171 of 200, 86% | 25 | 4 | 14.4 | 130 s |

**The pass mark was within 10 points of the teacher. DAgger is 8.5 points
behind (5.5 after round 5); cloning alone is 18.5 behind and does not pass.**

Three cautions on these numbers:

* **One map.** base1 is the only map the teacher plays through (report 3),
  so it is the only training map. The student has seen base1 with other
  seeds, not other maps. Nothing here says it can play a map it was not
  trained on.
* **The student is told the way.** It is the guided variant: it is given
  the direction and distance of the planner's next node at every step. The
  planner is the scripted teacher's, running in the game. What is learned is
  the moving, aiming, shooting, dodging and picking up, not the finding of
  the way.
* 200 episodes put about 3 points of noise on each rate. Round 5 being
  better than round 7 is within it.

## What is scripted and what is learned

Scripted: the teacher (reports 2 and 3), the guide the student is given,
and the labels. Learned: a recurrent network of 596,000 weights that
maps what the player perceives to the seven choices of an action.

## The student

`rl/q2rl/model.py`. Input, all of it something a player could know:

* its own state (32 numbers) and the weapon in hand;
* a 9 x 24 grid of rays over a 90 x 60 degree view: nearness, slope, and
  which of 10 kinds of thing was hit;
* up to 16 things in view with a clear line to the eye;
* the action it took at the step before;
* the guide: direction and distance of the planner's next node.

The rays go through three convolutions, the things in view through a shared
two-layer network and a maximum over them, everything is joined and mixed,
then a GRU of 256 units, then seven heads (3, 3, 3, 15, 11, 2 and 11
choices) and a value. No positions through walls, no map coordinates.

## Cloning

`python -m q2rl.bc --name bc --episodes 600 --epochs 40`

* Data: 600 episodes of the teacher on base1, seeds from 0: 781,244 steps,
  552 to the exit. Recorded in 77 s, 1.1 GB.
* Training: windows of 64 steps, 64 windows a batch, Adam at 3e-4, 40 passes
  over the data: 7,600 batches in 203 s.
* Final loss 1.54 summed over the branches. The student's most likely
  choice is the teacher's on 87% of steps for forward/back, 89% strafe, 99%
  jump/crouch, 79% yaw, 91% pitch, 99% fire, 100% weapon.
* 100 evaluation episodes: 66 to the exit with the most likely action, 68
  when sampling. (200 episodes, most likely action: 145, as in the table.)

## DAgger

`python -m q2rl.dagger --start bc --name dagger --rounds 8`

Each round the student plays 200 episodes of at most 3,000 steps, sampling
its actions, with the teacher's action played in its place on a share of the
steps (0.5, 0.3, 0.1, then 0). Every state is stored with the teacher's
action for it: the game gives that after every step, whoever acted, so
nothing is labelled afterwards. The student is then trained for 6 passes
over all the data so far and evaluated on 100 episodes.

| round | teacher's share | to the exit while collecting | steps of data | loss | evaluated: to the exit | died | out of time |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cloning | | | 781,244 | 1.54 | 66 of 100 | 21 | 13 |
| 0 | 0.5 | 176 of 200 | 1,035,736 | 1.60 | 75 | 17 | 8 |
| 1 | 0.3 | 167 | 1,298,418 | 1.65 | 70 | 25 | 5 |
| 2 | 0.1 | 144 | 1,546,788 | 1.73 | 84 | 15 | 1 |
| 3 | 0 | 143 | 1,836,121 | 1.79 | 81 | 17 | 2 |
| 4 | 0 | 127 | 2,084,566 | 1.74 | 80 | 18 | 2 |
| 5 | 0 | 114 | 2,313,523 | 1.68 | 84 | 14 | 2 |
| 6 | 0 | 153 | 2,568,326 | 1.62 | 78 | 21 | 1 |
| 7 | 0 | 143 | 2,796,684 | 1.57 | 79 | 18 | 3 |

The whole run took 24 min 47 s: about a minute of play and one to two
minutes of training a round, the rest evaluation.

What DAgger bought: running out of time fell from 13 in 100 to 1-3. The
cloned student gets stuck in places the teacher never stood in, and has no
data for getting out; after two rounds it has. Deaths did not fall: the
student dies about twice as often as the teacher (16% against 7.5%), with
or without DAgger.

The loss rises over the first rounds (1.54 to 1.79) and then falls: the
states the student gets itself into are harder to label right than the
teacher's own.

## Does the teacher label well where it did not drive?

This was the open question from report 2. The evidence here is indirect but
good: a student trained on the teacher's labels in its own states gets
better at reaching the exit, and stops getting stuck. Labels that were wrong
off the teacher's path would not do that. It has not been measured directly.

## Evaluation and the one card

Evaluation runs between training phases, never beside them: 28 servers and
the policy on the card, about 60 s for 100 episodes.

## Not done, not tested

* The unguided student. Other maps, held out or not. Other skills.
* Whether more rounds, a bigger network or longer windows would close the
  gap in deaths: nothing was tuned. One run of each, one seed.
* Windows. Nothing is pushed, so CI has not run.

## Disk

`~/q2pt-rl/`: 3.9 GB of data (the teacher's recording and eight rounds), 81
MB of navigation graphs, 5.4 GB of Python environment (PyTorch with its
CUDA libraries), weights under 0.1 GB. 9.5 GB of the 500 GB allowed.

## Rebase

Main has not moved (8eb4fa7): nothing to rebase.
