# Report 9: why the students fail, and PPO for longer

2026-11-10. Branch `rl-agent`. Step length 100 ms, skill 1 (normal),
single-player rules. Episodes are the legs of the base unit as report 8
defines them; the students are trained on the three legs after base1 and
never shown base1. The figures here can be set beside report 8's.

## The machine since report 8

The machine had been restarted into Linux 7.0.0-42 with no NVIDIA module
for that kernel: an install of it begun on 9 November had not finished.
With the owner's leave that install was completed (`apt-get --fix-broken
install`); the driver went from 595.84 to 595.99.02, and the module loaded
without a restart. The same change removed the module for the 7.0.0-28
kernel.

Before the card was back, the three PPO students of report 8 were played
again on the processor: 300 episodes each, and every count (exit, died,
out of time, another exit, on every leg) is the one the card gave in
October. Full-precision play by the likeliest action is the same on both.

Of Docker's two containers one is running. The other failed to start when
the machine came up on 9 November; it was not touched.

## How the students' episodes end

`rl/tools/deaths.py` plays the episodes `q2rl.evaluate` plays and keeps,
for each that does not reach the exit, the cause, the place and the last 60
steps. The three PPO students of report 8 (3.0 M steps) and the teacher,
100 episodes a leg, likeliest action; the students' three figures are the
three seeds:

| base3@base2a | teacher | students |
| --- | --- | --- |
| reached the exit | 86 | 33, 20, 37 |
| out of time | 0 | 20, 21, 10 |
| killed by a gunner | 3 | 14, 15, 12 |
| by a parasite | 2 | 10, 17, 6 |
| by a guard of any kind | 7 | 9, 13, 12 |
| by a laser | 0 | 7, 0, 15 |
| by infantry | 0 | 5, 11, 3 |
| otherwise | 2 | 2, 3, 5 |

| base2@base3b | teacher | students |
| --- | --- | --- |
| reached the exit | 84 | 54, 51, 54 |
| killed by a gunner | 14 | 18, 34, 27 |
| out of time | 2 | 23, 12, 11 |
| otherwise | 0 | 5, 3, 8 |

On base2@base1 the students are near the teacher (87, 89, 82 against 95);
what is lost there is out of time (10, 3, 4), a door or lift that crushes
(3, 3, 2), and for one seed the wrong exit (8).

Three things stand out.

**Out of time is standing still.** Of the students' 114 episodes that ran
out of time, 95 ended with the player doing nothing at all for the last
60 steps (every branch at rest), and 17 more standing on one spot. On base3
most stand in one place, a low floor near (848, 243), where the teacher
would have them move. This is the fault report 8 found on base1, on the
maps the students trained on: play by the likeliest action that comes to
"do nothing" with "did nothing" as its last action stays there. On the
last leg it is partly something else: see the temperature table below.

**Lasers are not perceived in time.** The student is told of a beam only
by the eight marks about its feet, one stride (40 units) out. A step at a
run is 30 units, and base3's beams kill in one step: in every laser death
the mark came up one or two steps before the end, and no damage was
reported before it. The rays do not see beams. The teacher never dies to
them because the planner routes round a beam that is on, so it also never
shows the student what to do near one. This is a gap in the observation,
and closing it (a ray kind for beams, or a longer look at the ground) means
a new observation and recording the teacher again. Not done here.

**Gunners are the most of it.** On the last leg they kill the students
where they kill the teacher, the yard by (128, -1408), about twice as
often.

## Crouching, and base1

Report 8: the PPO students stop on base1 at a low passage where the
teacher crouches. Two measurements.

How the teacher crouches, in its own recorded play (base1: 56 episodes
recorded today; the training legs: the 900 of seed 0):

| the teacher's crouches | base1 | the training legs |
| --- | --- | --- |
| one step long | 3.0 an episode | 15.9 |
| 2 to 9 steps | 1.4 | 7.7 |
| 10 steps or more | 2.3 | 0.8 |

The base1 column is from an older recording of 600 episodes; today's 56
have 113 held crouches, 2.0 an episode. A one-step crouch is the teacher
slowing at an edge. A crouch held for a second or more is mostly
a low passage: base1 has two an episode, the training legs few, and those
in few places.

What the students make of it. The teacher's play was run through each
student (the student sees what the teacher saw and is told the teacher's
last action), and the probability it gives to crouching read off:

| where the teacher holds a crouch | DAgger students | PPO students (3.0 M) |
| --- | --- | --- |
| base1, the first step: crouch is the likeliest | 7%, 11%, 2% of 113 | 2%, 0%, 0% |
| base1, all its steps: probability of crouch | 0.71, 0.85, 0.64 | 0.65, 0.27, 0.46 |
| training legs, the first step: likeliest | 83%, 83%, 75% of 12 | 83%, 58%, 42% |
| training legs, all steps: probability | 0.98, 0.99, 0.98 | 0.86, 0.79, 0.92 |

So on a map they have not seen the students almost never *begin* a crouch
as their likeliest action, though once crouched (by the teacher, in this
test) they mostly stay down. On the legs they trained on they begin it:
they know those places, not low openings in general. And PPO lowers the
probability of crouching everywhere. Getting through base1's passage by the
likeliest action is therefore luck in how the player comes to it, which is
what the figures below show again.

## A third way of playing: cooled

`q2rl.evaluate --cool T` draws each action from the policy sharpened by a
temperature T (the logits divided by T). The three PPO students of report
8, 100 episodes a leg, mean of the three ± their spread:

| play | base2@base1 | base3@base2a | base2@base3b | base1, held out |
| --- | --- | --- | --- | --- |
| likeliest action | 86% ± 4 | 30% ± 9 | 53% ± 2 | 0% ± 1 |
| cooled to 0.25 | 88% ± 7 | 38% ± 2 | 52% ± 3 | 42% ± 33 |
| cooled to 0.5 | 89% ± 3 | 26% ± 2 | 41% ± 7 | 70% ± 13 |
| cooled to 0.75 | 88% ± 2 | 11% ± 7 | 28% ± 6 | 73% ± 5 |
| drawn (1.0) | 84% ± 6 | 5% ± 3 | 15% ± 6 | 71% ± 5 |

* On base3 a little randomness takes the episodes out of time from 17 a
  hundred to 6, and the exits from 30 to 38. More than a little loses the
  fights: deaths go from 53 a hundred to 57, 68, 82, 93.
* On the last leg the episodes out of time do not go away with randomness
  (15, 17, 21, 26, 29 a hundred): there the player is somewhere with no way
  out, the pit the teacher too is sometimes left in (report 8), and more
  randomness puts it there more often.
* 0.25 is the temperature at which nothing is lost on the trained legs, and
  `tools/run_seed.py` now evaluates every player a third time at it
  ("cool"). **It does not do what I proposed it for.** The held-out figure
  at 0.25 is 74, 42 and 9 for the three seeds, as wide a spread as before,
  and for the longer-trained students below it comes out the other way
  round from the likeliest action. It is a useful second reading; it is
  not a robust one.

## PPO for 8.0 M steps

The same PPO stage as report 8 in every setting but its length, 8.0 M steps
in place of 3.0 M, from the same three DAgger students; the weights kept
are those of the best of 17 validations (at 6.0 M, 6.0 M and 7.5 M steps).
100 evaluation episodes a leg, three seeds, likeliest action; in brackets
cooled to 0.25, then drawn:

| player | base2@base1 | base3@base2a | base2@base3b | base1, held out |
| --- | --- | --- | --- | --- |
| teacher | 95% | 86% | 84% | 100% |
| DAgger | 81% ± 9 (87% ± 2, 66% ± 7) | 20% ± 9 (23% ± 7, 1% ± 1) | 8% ± 2 (8% ± 3, 2% ± 2) | 77% ± 25 (72% ± 12, 55% ± 6) |
| PPO, 3.0 M | 86% ± 4 (88% ± 7, 84% ± 6) | 30% ± 9 (38% ± 2, 5% ± 3) | 53% ± 2 (52% ± 3, 15% ± 6) | 0% ± 1 (42% ± 33, 71% ± 5) |
| PPO, 8.0 M | 87% ± 2 (95% ± 3, 90% ± 5) | 40% ± 8 (49% ± 6, 12% ± 6) | 59% ± 4 (57% ± 7, 27% ± 9) | 88% ± 6 (24% ± 30, 73% ± 2) |

The cloned students, cooled: 47% ± 15, 0% ± 0, 1% ± 1 and, held out,
15% ± 13; their other figures are report 8's.

On the legs trained on:

* **More steps help, slowly.** base3 goes from 30% to 40% and the last leg
  from 53% to 59%; drawn play about doubles on both. The students are
  still far from the teacher's 86% and 84%.
* The share of *training* episodes (drawn actions) that reach the exit was
  still rising at the end: about 21% at 0.5 M steps, 37% at 3 M, 42% over
  the last two million. The validation it is checked by, 84 episodes, moves
  by ten exits from one check to the next, so which weights are kept is
  partly luck.
* How the 8.0 M students' episodes end on base3, likeliest action: 47, 56
  and 40 die (3.0 M: 47, 59, 53) and 18, 8 and 11 run out of time (20, 21,
  10). The causes were not broken down again.

On base1:

* **By the likeliest action the 8.0 M students reach the exit in 93, 88 and
  82 of 100, where the 3.0 M students reached it in 0, 0 and 1.** Cooled to
  0.25 they reach it in 5, 59 and 9.
* It is the same passage. Seed 0, cooled: 95 of 100 episodes end out of
  time within 60 units of (-352, -416), standing, with the teacher telling
  it to crouch. By the likeliest action the same student stops there twice
  in 100. Nothing was learned about low openings: the 8.0 M students
  happen to come at this one in a way that gets them through, and a small
  disturbance undoes it.
* **So the held-out figure by the likeliest action is not to be trusted in
  either direction**: 0% was not "cannot play base1" and 88% is not "can".
  Drawn play is the steadier reading of it, and has not moved: 55% for the
  DAgger students, 71% at 3.0 M, 73% at 8.0 M.

The unit as one run, each leg from what the student itself carried
(`tools/unit.py`, 50 runs each, likeliest action): **10, 3 and 4 of 50 get
through the whole unit**; the teacher, 74 of 100. Summed over the three
students:

| leg | runs that began it | reached its exit |
| --- | --- | --- |
| base1 | 150 | 136 |
| base2@base1 | 136 | 72 (53%) |
| base3@base2a | 72 | 32 (44%) |
| base2@base3b | 32 | 17 (53%) |

The second leg is where a student playing on its own account loses most:
53%, against 87% when it starts with what the teacher carried out of
base1. It arrives with less.

## What this says to do next

In the order I would do them; none is begun.

1. **Let the student see beams**, and re-record. It is the one fault found
   here that no amount of training can mend: 7 in 100 of base3's episodes.
2. **Start training episodes from the student's own carry** as well as the
   teacher's. The unit run shows the students are weaker than their table
   on any leg they reach on their own.
3. **A low passage in the training set**, or base1 put into training and
   another map held out. As it stands the held-out map tests one skill the
   training legs barely contain, and tests it as a coin toss.
4. More PPO steps would add a little more. I would not spend them before
   1 and 2.

## Housekeeping

* GPU: training and evaluation took the card in turn. The first
  measurements (`deaths.py` on the report 8 students and the teacher) ran
  on the processor, the card being out.
* Time: 8.0 M steps of PPO take about 90 minutes a seed, and a seed's six
  evaluations about 20.
* Disk: `~/q2pt-rl/` is 45 GB of the 500 GB allowed.
* Docker: see above; nothing of Docker's was touched.
* Not changed: the game, the teacher, the observation, the reward. The
  changes are in `rl/` only: `evaluate.py --cool`, `tools/deaths.py`, a
  third evaluation in `run_seed.py` and a row for it in `table.py`.
* `rl/tests/test_layout.py` was not run: the environment has no pytest,
  and nothing it checks was changed.
* Rebase on main: main has not moved since 086f2dd, so there was nothing to
  rebase onto and no conflict.
* Weights: `~/q2pt-rl/weights/u1L_ppo_s{0,1,2}.pt`; figures in
  `~/q2pt-rl/results/u1L/` and, for the report 8 students,
  `~/q2pt-rl/results/u1/` (`*_cool*.json`, `deaths/`).
* No new demos.
