# Report 5: PPO from imitation against PPO from nothing

2026-10-10. Branch `rl-agent`. Machine as in report 0: one RTX 4090, 28
server processes. base1, skill 1, step length 100 ms. The guided student.

## Result

The same 200 evaluation episodes as report 4 (seeds the training never
sees, 6,000 steps allowed):

| player | most likely action | sampling its actions |
| --- | --- | --- |
| teacher (scripted) | 182 of 200, 91% | |
| cloned student | 145, 72% | 142, 71% |
| DAgger student | 165, 82% | 138, 69% |
| **PPO from the DAgger student, 3.0 M steps** | 171, 86% | **195, 98%** |
| PPO from random weights, 3.0 M steps | 0 of 56 | 0 of 56 |

* **Fine-tuned and sampling, the student reaches the exit more often than
  the teacher that taught it** (195 against 182 of the same 200 episodes),
  and sooner: 85 s an episode against the teacher's 136 s. 5 deaths, none
  out of time.
* **From random weights, with the same reward and the same 3.0 million
  steps, PPO reaches the exit in no episode**, in training or evaluation.
  It learns not to die: 0 deaths and 0 kills in the final evaluation, every
  episode standing out its time. What the teaching buys on this budget is
  the whole of the result.
* Playing its most likely action the fine-tuned student is no better than
  DAgger's (86% against 82%): it gets stuck, 23 episodes out of time.
  Sampling, it never does. PPO trains the policy that samples, and the
  cloned and DAgger students are the other way round: sampling makes them
  die more (62 of 200 for DAgger against 32).

## Learning curves

`~/q2pt-rl/plots/curves.png` (drawn by `rl/tools/curves.py`): both runs on
the same axes, with the teacher, the cloned and the DAgger students as
levels. The numbers in it:

| steps | from DAgger: evaluation, of 56 | from DAgger: last 200 training episodes | from random: evaluation | from random: training |
| --- | --- | --- | --- | --- |
| 0 | 45 | | 0 | |
| 0.5 M | 40 | 79% | 0 | 0% |
| 1.0 M | 53 | 83% | 0 | 0% |
| 1.5 M | 55 | 94% | 0 | 0% |
| 2.0 M | 47 | 94% | 0 | 0% |
| 2.5 M | 50 | 97% | 0 | 0% |
| 3.0 M | 47 | 94% | 0 | 0% |

The evaluations play the most likely action and the training episodes
sample, which is why the training column is above the evaluation one from
1.5 M on. 56 episodes put about 5 points of noise on an evaluation; the dip
at 0.5 M is while only the value head has been trained for 0.2 M steps and
the teacher's penalty is at its strongest, and is not outside that noise.

## Settings

`python -m q2rl.ppo --name ppo_ft --start dagger` and
`python -m q2rl.ppo --name ppo_scratch`, both `--steps 3000000`.

* 28 environments, 128 steps each between updates (3,584 steps an update),
  episodes of at most 3,000 steps while training.
* GAE with gamma 0.995 and lambda 0.95; clip 0.2; 3 passes over each batch
  in 4 parts; Adam at 2e-4; entropy weight 0.003; value weight 0.5; gradient
  norm clipped at 0.5.
* Reward per step: +0.05 for each second gained along the planner's route
  (at most 0.2 s a step), +0.005 per point of damage dealt, +0.5 a kill,
  +10 for the exit, -0.01 per point of damage taken, -5 for dying, -0.002
  for the step.
* From imitation only: the first 200,000 steps train the value head alone;
  a penalty for straying from the teacher's action (cross entropy, weight
  0.5) falls in a straight line to nothing at 1.5 M steps.
* From random weights: neither of those. Same reward, same steps, same
  everything else.
* 3.0 M steps is 83 hours of game time. Each run took 19 minutes, of which
  about 5 were the seven evaluations, which run while training waits.

## What should be kept in mind

* **One run of each, one seed, one map.** No error bars. The student was
  trained and evaluated on base1; the evaluation seeds differ from the
  training seeds, the map does not.
* **The student is guided** and the reward pays for progress along the
  planner's route. The fine-tuned student beats the scripted teacher at
  playing, not at finding the way: the way is still the teacher's.
* The from-nothing run has the guide and the route reward too, and still
  finds nothing in 3 M steps. A longer run was not tried.
* Why the fine-tuned student is faster than the teacher was not looked
  into. It kills as many monsters (15.4 against 15.1 of 17).

## Demo

`~/q2pt-rl/demos/student_ppo_base1.dm2`: the fine-tuned student, sampling,
on evaluation seed 500000: the exit in 86.9 s, 15 of 17 monsters, 100
health. 869 frames, passes `dm2check.py`. Recorded with
`rl/tools/record.py`. Rendered whole with `pt_render` in the full client at
10 frames a second: 870 frames, 87.0 s, no error. Four stills were looked
at (it shoots guards with the blaster and the shotgun and picks up armour);
nobody has watched it as a film.

## Not done, not tested

* More than one seed of anything. Other maps. Other skills.
* A from-nothing run long enough to learn something.
* Windows. Nothing is pushed, so CI has not run.

## Disk

`~/q2pt-rl/`: 3.9 GB of data, 5.5 GB of Python environment, 81 MB of
navigation graphs, 30 MB of weights, logs and plots under 0.1 GB. 9.6 GB of
the 500 GB allowed.

## Rebase

Main has not moved (8eb4fa7): nothing to rebase.
