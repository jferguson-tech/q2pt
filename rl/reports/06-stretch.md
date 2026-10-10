# Report 6: the stretch goals

2026-10-10. Branch `rl-agent`. Machine and settings as in report 5.

Three things were asked for after milestone 5. One was tried.

## The unguided student: tried, on one map

The same network with the guide set to zero: it is not told where the
planner's route goes. Cloned from the same 600 teacher episodes, then 6
rounds of DAgger. The same 200 evaluation episodes on base1, most likely
action:

| student | reaches the exit | died | out of time |
| --- | --- | --- | --- |
| guided, cloned | 145 of 200, 72% | 36 | 19 |
| guided, DAgger | 165, 82% | 32 | 3 |
| unguided, cloned | 66, 33% | 75 | 59 |
| unguided, DAgger | 118, 59% | 40 | 42 |

Without the guide the cloned student agrees with the teacher less (loss
2.13 against 1.54; strafe right on 74% of steps against 89%) and finishes
less than half as often. DAgger nearly doubles it.

**This is not finding the way.** There is one map. A student that reaches
base1's exit unguided has learned base1's route from the look of its rooms.
Whether any of that carries to another map could not be tested: there is no
other map the teacher can teach.

The unguided run was also unsteady. By round, of 100: 60, 60, 41, 51, 46,
58. When the student drove alone and sampled its actions (rounds 3 to 5) it
reached the exit in 1, 1 and 3 of 200 collecting episodes of 3,000 steps,
so most of what was added to the data were states of a lost player, and the
loss rose from 2.30 to 2.90. It was not fine-tuned with PPO.

`python -m q2rl.bc --name bc_unguided --unguided` then
`python -m q2rl.dagger --start bc_unguided --name dagger_unguided --rounds 6`:
4 min and 22 min.

## Maps held out of training: not possible yet

It needs at least two maps the teacher plays through, one to train on and
one to hold out. There is one (report 3).

## Playing through a unit across map changes: not done

An episode still ends at the map change. Carrying on needs the level files
the game writes on a change, so a game folder per server process, and a
teacher that knows which side map holds the key it lacks. Report 3's table
shows why it matters: most maps of a hub cannot be finished alone.

## What would move this forward

In order of what it would unlock: the teacher on more maps (weapons carried
from map to map would deal with most of the "outgunned" failures, and the
planner needs trains, teleporters and walls that are shot away); then
training on several maps; then holding one out.

## Disk

`~/q2pt-rl/`: 7.1 GB of data (3.2 GB of it the unguided rounds), 5.5 GB of
Python environment, 81 MB of navigation graphs, 40 MB of weights. 12.8 GB
of the 500 GB allowed.

## Rebase

Main has not moved (8eb4fa7): nothing to rebase.
