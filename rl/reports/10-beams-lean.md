# Report 10: beams in the rays, and starting with less

2026-11-11. Branch `rl-agent`. Step length 100 ms, skill 1 (normal),
single-player rules. Episodes are the legs of the base unit as report 8
defines them; the students are trained on the three legs after base1 and
never shown base1.

Two changes were made after report 9 and three students trained from
nothing with both (teacher recording, cloning, DAgger, PPO for 8.0 M
steps; three seeds). **Neither did what it was made for in full: the first
did nothing for laser deaths, the second helped the leg it was aimed at
and not the unit.**

## What was changed

1. **The rays see laser beams.** A ray that passes close by a beam that is
   switched on reports the beam (a new kind, 10) and the distance to it,
   in place of what lies behind (`Haz_RayBeam` in `game/g_rl_hazard.c`).
   The shared block is version 8 and has 11 ray kinds, so **no weights from
   before this report load any more**, and the teacher was recorded again.
2. **Lean starts.** Half of all training episodes (teacher's, DAgger's,
   PPO's) start a leg with less than the teacher carried into it: health
   between half and all of it, armour and ammunition anywhere from none to
   all, each weapon but the blaster lost with probability one half
   (`carry.draw`, `--lean 0.5`). Evaluation never does this: its episodes
   start as they did in reports 8 and 9, so the tables can be compared.

## The legs, one at a time

100 evaluation episodes a leg, three seeds, likeliest action; in brackets
cooled to 0.25, then drawn. The first PPO row is report 9's.

| player | base2@base1 | base3@base2a | base2@base3b | base1, held out |
| --- | --- | --- | --- | --- |
| teacher | 95% | 86% | 84% | 100% |
| PPO 8.0 M, report 9 | 87% ± 2 (95% ± 3, 90% ± 5) | 40% ± 8 (49% ± 6, 12% ± 6) | 59% ± 4 (57% ± 7, 27% ± 9) | 88% ± 6 (24% ± 30, 73% ± 2) |
| cloned | 20% ± 12 (28% ± 13, 6% ± 2) | 0% ± 0 (0% ± 0, 0% ± 0) | 1% ± 1 (0% ± 1, 0% ± 0) | 42% ± 37 (21% ± 16, 15% ± 1) |
| DAgger | 76% ± 7 (84% ± 2, 63% ± 13) | 32% ± 9 (29% ± 5, 3% ± 3) | 12% ± 4 (10% ± 4, 2% ± 2) | 56% ± 49 (80% ± 12, 60% ± 10) |
| PPO 8.0 M | 89% ± 4 (90% ± 2, 81% ± 2) | 40% ± 11 (49% ± 6, 6% ± 4) | 61% ± 8 (55% ± 13, 16% ± 15) | 30% ± 52 (47% ± 38, 68% ± 5) |

* **On the legs trained on, nothing moved.** 89%, 40% and 61% against 87%,
  40% and 59%. Half the training episodes being harder cost nothing there
  and gained nothing.
* The DAgger students are a little better on base3 than report 9's (32%
  against 20%), which PPO does not keep.
* base1, held out, by the likeliest action is 0, 0 and 90 of 100 for the
  three seeds, and cooled 8, 51 and 83. It is the low passage again
  (report 9): two of the three students stand at it until time runs out.
  Drawn play, the steadier reading, is 74, 64 and 66, where report 9 had
  73% ± 2. Nothing here was meant to change it and nothing did.

## Lasers: not mended, and why

How the PPO students' episodes end on base3@base2a, 100 episodes each,
likeliest action, three seeds:

| base3@base2a | 3.0 M, report 9 | 8.0 M, this report |
| --- | --- | --- |
| reached the exit | 33, 20, 37 | 40, 51, 30 |
| killed by a laser | 7, 0, 15 | 16, 3, 0 |
| by a gunner | 14, 15, 12 | 13, 13, 13 |
| by a parasite | 10, 17, 6 | 8, 6, 9 |
| by a guard of any kind | 9, 13, 12 | 7, 11, 14 |
| by infantry | 5, 11, 3 | 7, 7, 7 |
| out of time | 20, 21, 10 | 9, 6, 15 |
| otherwise | 2, 3, 5 | 0, 3, 12 |

Laser deaths are 19 in 300 where they were 22. Seed 2's "otherwise" has 6
drowned and 4 killed by a barrel.

The beams are in the rays: a teacher episode on base3 has them in 44
steps, from as far as 720 units. But one student (seed 0) was played again
with the rays recorded, and of its 13 laser deaths in 84 episodes **12 had
no beam in any ray in the 14 steps before the end**. What happens:

* base3 has two beams, both along y = -400 from x = -266 westward, at knee
  and head height. They are on from the start and go off when one gunner,
  who stands 70 units from them, is killed.
* Every laser death is on that line. In the steps before it the student
  is firing and moving *backwards* (the forward branch at "back", the fire
  branch on), with the teacher's action for those steps being to stand or
  go the other way. It is fighting in the room in front of the beams and
  backs into them.
* The rays cover the view, 24 by 9 through what the player is looking at.
  A beam behind the player is in none of them. The only thing that tells
  of a beam behind is the eight marks about the feet, one stride out,
  which is the one or two steps of warning report 9 found.

Report 9 took "the mark comes one or two steps before the end" to mean
the beams were seen too late and proposed the rays. That was a wrong
reading of a right measurement: the beams are not ahead of the student
when it dies. What is missing is a sense of harm *all round* that reaches
further than a stride: for instance the distance to the nearest harmful
ground or beam in each of the eight ways about the feet, out to a few
hundred units, in place of the yes or no at 40.

The beam rays are harmless and are left in.

## The unit as one run: the second leg is better, the unit is not

`tools/unit.py`, 50 runs a student, each leg from what the student itself
carried out of the last. By the likeliest action:

| | report 9 (8.0 M) | this report |
| --- | --- | --- |
| through the whole unit, of 50 | 10, 3, 4 | 0, 0, 7 |

Seeds 0 and 1 stop at base1's passage in all 50 runs, so by the likeliest
action only seed 2 says anything about lean starts. To have the other two
as well, `unit.py` was given `--cool` and the unit played again cooled to
0.25 (50 runs each: 2, 3 and 2 get through). Both ways of playing summed,
beside report 9's students by the likeliest action:

| leg | report 9: began, reached the exit | this report, likeliest (seed 2) | this report, cooled (3 seeds) |
| --- | --- | --- | --- |
| base1 | 150, 136 | 50, 46 | 150, 74 |
| base2@base1 | 136, 72 (53%) | 46, 41 (89%) | 74, 58 (78%) |
| base3@base2a | 72, 32 (44%) | 41, 11 (27%) | 58, 19 (33%) |
| base2@base3b | 32, 17 (53%) | 11, 7 | 19, 7 |

* **The second leg from the student's own carry goes from 53% to 89% and
  78%.** That is the leg report 9 found weakest and the thing lean starts
  were for; started with the teacher's carry the same leg is 89%, so the
  gap has closed. It is one seed by the likeliest action and three cooled,
  and the two ways of playing are not the same as report 9's one.
* **base3 from the student's own carry is no better and may be worse**:
  27% and 33% against 44%, and 40% when started with the teacher's carry.
  The counts are small (41 and 58 runs). I have not looked into what the
  students arrive on base3 with.
* So the whole unit is where it was or below: 7 of 50 for the one seed
  that passes base1, and 2 or 3 of 50 cooled, against 10, 3 and 4.
* The unit by the likeliest action is now decided, for two seeds of three,
  by the base1 passage and nothing else.

## What this says to do next

None is begun.

1. **Decide what to do about base1 and crouching.** It is still open from
   report 9 and it now hides everything else in the unit run: either base1
   goes into training and another map is held out, or a low passage is
   added to what the students train on. This is a choice about what
   "held out" is to mean, so it is the owner's.
2. **A sense of harm all round**, as above. It means a new observation and
   training again from the teacher's recording.
3. **Look at base3 from the student's own carry**: what it arrives with
   and how those runs end. `deaths.py` does not do this yet; `unit.py`
   would have to keep the endings.
4. More PPO steps are still the least of it.

## Housekeeping

* GPU: training and evaluation took the card in turn, in one script. The
  cooled unit runs and the first unit runs were on the processor.
* Time: a seed from nothing (recording 900 teacher episodes, cloning,
  eight DAgger rounds, 8.0 M steps of PPO, eighteen evaluations) took
  between 3 h 15 and 3 h 55.
* Disk: `~/q2pt-rl/` is 61 GB of the 500 GB allowed, 37 GB of it the
  datasets of the old observation in `old-layout7/`, which nothing can
  read any more and can be deleted.
* Docker: one container running (two days up) and one stopped since
  before report 9; neither was touched.
* Tested: `rl/tests` (7 pass). The game was built on Linux with warnings
  as errors before the run. The Windows build was not compiled here.
  GitHub's Linux job cannot check this branch until the pull request that
  pins the C standard (#65) is merged: its compiler rejects
  `game/q_shared.h` as it stands on main.
* Rebase on main: main has not moved since 086f2dd; nothing to rebase
  onto, no conflict.
* Weights: `~/q2pt-rl/weights/u2_{bc,dagger,ppo}_s{0,1,2}.pt`; figures in
  `~/q2pt-rl/results/u2/`, the episode endings in its `deaths/`, the unit
  runs in `~/q2pt-rl/logs/unit_u2_ppo_s*.txt` and `*_cool.txt`.
* No new demos.
