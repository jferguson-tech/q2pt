# Report 7: the teacher on more maps, and students moved to them

2026-10-10. Branch `rl-agent`. Machine as in report 0. Step length 100 ms,
skill 1 (normal), single-player rules, each map played alone from a fresh
start with the blaster only.

## Result

**The teacher now plays three maps through, not one: base1, base3 and
mintro.** Five more were asked for; two more is what there is.

| map | what it has | before | now | time, kills (median, mean) |
| --- | --- | --- | --- | --- |
| base1 | the first map | 96 of 100 | 98 of 100 | 105 s, 15.9 of 17 |
| base3 | water, slime, a key, parasites | 12 of 28 | 86 of 100 | 89 s, 8.3 of 38 |
| mintro | lava, narrow ledges, a switch behind bars, berserkers | 0 of 28 | 56 of 100 | 212 s, 28.5 of 36 |

100 seeded episodes each (seeds 0-99), 6,000 steps allowed. "Before" is
report 3's table. **Episodes that run out of time: 2 of those 300**: on
base1 one stands before an item it cannot get to, on mintro one stands
shooting from a hole it was knocked into. In report 3's table the teacher
ran out of time in 13 of 28 on base2, 4 on base3 and 28 on train.

What still fails, in those 300 episodes:

| map | killed by a monster | lava | drowned | a barrel | out of time |
| --- | --- | --- | --- | --- | --- |
| base1 | 1 (shotgun guard) | | | | 1 |
| base3 | 6 (enforcer 4, parasite 2) | | 7 | 1 | |
| mintro | 23 (berserker 18, guard 2, mutant 2, icarus 1) | 20 | | | 1 |

mintro's lava deaths are no longer walks off a ledge: in the traces looked
at, the player is knocked off by a hit while it stands fighting near an
edge, or lands badly after one. Not every one of the 20 was looked at. Its
berserker deaths are three berserkers met with the blaster or a machinegun
that has run dry. base3's drownings are a long way under water with the
air running out; the teacher does not count its air.

## Most maps the teacher "finishes" are not tests

This was found while moving the students, and it changes what report 3
said. **On most of the maps where the teacher reaches an exit, a player
with no sense reaches one too.** `rl/tools/walker.py` plays each map with
one player that only walks forward and one that acts at random, 14
episodes each, 3,000 steps:

| | maps |
| --- | --- |
| acting at random reaches an exit, usually within 10 s | base2, train, ware1, ware2, jail2, jail3, jail4, jail5, security, mine1, mine2, mine3, mine4, fact2, power2, cool1, waste1, waste2, waste3, hangar2, lab, command, strike, city2, city3 |
| walking forward reaches the exit, always at 31 s | fact3 (a secret level on a clock: it ends by itself) |
| neither ever does | base1, base3, bunk1, jail1, mintro, fact1, power1, biggun, hangar1, space, city1, boss1, boss2 |

In a hub the player arrives beside the way back, and played alone that
map's "exit" is a few steps from the start. So of the 39 maps, **13 are
tests, and the teacher passes three of them**. The other ten:

| map | the teacher's commonest end, 28 episodes |
| --- | --- |
| bunk1, biggun, boss1 | no plan: the graph does not reach an exit (trains and the like) |
| jail1, fact1, hangar1, city1 | killed by gunners, enforcers, a gladiator, with the blaster |
| power1, boss2 | killed by a supertank, by Jorg |
| space | killed by a trigger that hurts (28 of 28) |

I had picked train, ware1 and fact2 as three of the five, and worked on
them, before the students showed they could be left in three seconds. The
teacher there was walking off for weapons and dying to gunners on the way
back to an exit it started beside; an item is now worth little when the way
out is a few steps off.

The full table of all 39 maps is at the end.

## What was asked for, and where it stands

* **Lava.** mintro: 27 of 28 episodes ended in lava; now 20 of 100. See
  "Trial steps" below.
* **Lasers.** `game/g_rl_hazard.c`: a link through a beam is shut while the
  beam is on, and the planner looks for what switches it. On fact1 this
  took deaths by laser from 27 of 28 to none or one. **But fact1 is still
  not finished** (gunners, 22 of 28), and none of the three maps the
  teacher finishes has a beam across its way. So the laser work is tested
  only as "it no longer walks into them".
* **Weapon pickups.** A weapon the player lacks is worth 45 s of walking
  (was 10). On base3 the teacher fetches the shotgun and the machinegun, on
  mintro the machinegun, the hyperblaster and the grenade launcher. It does
  not plan a fight around having them: three berserkers are met with what
  is in hand.
* **Stuck until time runs out.** 2 of 300 on the three maps, above. The
  causes found and removed are listed below. On maps the teacher does not
  finish it still stands with no plan (base2, bunk1 and others in the last
  table).

## What was changed in the teacher

Each line is a cause found in a trace and the change made for it.

**Trial steps.** At 100 ms a step goes 30 units and stopping takes 17 more;
the graph was grown from standing starts at each node. So the player
overran ledges, and began drops at a run that only work from rest.

* Before the feet are told anything on the ground, the step is tried with
  `gi.Pmove` from the player's own place and speed: one step, then
  stopping. It must come to rest on a floor, unhurt, no more than a stair
  down, at a place the graph has a node with a way on. Failing that it is
  tried crouched, then to either side, then the feet stay.
* A jump, a walk off an edge, a crouched drop or a swim out of water is
  begun only when a trial of the whole move from where the player is ends
  at the link's far node or nearer the goal, and with the view square to
  it. Until then: to the node, crouched, and stand.
* A link whose trial from its node, standing, ends in harm or goes nowhere
  is struck out for the episode and the way worked out without it. (fact1's
  graph has links onto a platform that is not there when the map is
  played.)
* In the air the feet go the way meant only if a trial of that lands
  safely.

**Stalls.**

* The teacher shot for ever at monsters not yet brought in by a trigger:
  they have a place and health but nothing to hit. (train: 28 of 28.)
* A switch to be shot was aimed at its middle from one of the four nearest
  nodes; mintro's is behind bars and the nearest nodes are behind the door
  it opens. Now any part of it that the gun's line reaches, with a margin,
  from any node that has one.
* On a door or lift the node taken was always the one for its home end.
* A swim out over a bank works only facing it, from the right depth.
* Water nodes lie above one another and the nearest in plan was the wrong
  one.
* With no step that passed its trial the teacher stood for good: on a perch
  off the graph it now takes the step down that ends nearest the goal.
* It turned aside for items from which the way out could not be taken up
  again, and dived for items until it drowned.

**Fighting.** This is where most of the gain on base1 and base3 came from.

* The aim was taken on where the monster was. A player dodging at a run
  sees a monster 250 units off swing through 7 degrees a step, so the view
  was never on it: in one trace, 55 steps against one guard with a handful
  of shots fired. The aim is now on where both will be a step on, and the
  shot is judged on the view after the step's turn. base1: 91% to 100% of
  56 on that change alone.
* With two monsters about equally near the aim swung from one to the other
  and hit neither. The one fought a step ago is preferred.
* Weapons were chosen by distance bands and changed back and forth across
  200 units, firing nothing meanwhile.
* A monster that would take more than 4 s with what is in hand is not stood
  before: the feet keep to the route. One that fights hand to hand is
  backed away from when close and in the way.

## The students

Trained on the two new maps the teacher plays, base3 and mintro, with
**base1 held out**: no student saw base1. Three whole runs, each from its
own seed (the weights' start, the batches, the teacher's 900 episodes, the
episodes played in DAgger and PPO). Each student is then played for 100
episodes a map on seeds no training run uses (500000 up; PPO's own checks
use 400000 up). The figure is the share that reached the exit: the mean of
the three runs, and after the ± the spread between them (standard
deviation of the three rates, n-1). **The main figure is with the likeliest
action taken each step**; beside it, with actions drawn. The teacher's row
is the same 100 episodes a map; on these seeds it finishes mintro less
often than on seeds 0-99 above (43 against 56 of 100).

| player | base3 | mintro | base1, held out |
| --- | --- | --- | --- |
| teacher (the same episodes) | 83% | 43% | 99% |
| cloned, likeliest | **26% ± 3** | **0% ± 0** | **0% ± 0** |
| cloned, drawn | 12% ± 5 | 0% ± 0 | 38% ± 20 |
| DAgger, likeliest | **46% ± 8** | **0% ± 0** | **19% ± 33** |
| DAgger, drawn | 29% ± 6 | 0% ± 0 | 55% ± 8 |
| PPO from DAgger, likeliest | **72% ± 11** | **0% ± 0** | **31% ± 44** |
| PPO from DAgger, drawn | 54% ± 11 | 0% ± 0 | 66% ± 18 |

The three runs one by one, exits of 100, likeliest action:

| player | base3 | mintro | base1, held out |
| --- | --- | --- | --- |
| cloned | 25, 29, 23 | 0, 0, 0 | 0, 0, 0 |
| DAgger | 37, 49, 52 | 0, 0, 0 | 0, 57, 0 |
| PPO | 76, 60, 80 | 0, 0, 0 | 0, 81, 12 |

What this says:

* **base3**: each stage adds, and PPO comes to within 11 points of the
  teacher on the mean (72% against 83%), with one run of three at 80%.
* **mintro: no student ever finishes it**, 0 of 1,800 episodes. Twelve
  episodes of one DAgger student were looked at: ten end in lava at the
  first island, between 100 and 350 steps in. That is where the teacher
  creeps to a node, stands, squares its view and jumps, each on a trial of
  the move the student has no way to make. The observation of the ground
  about the feet, added for this, did not change it (before it, one seed:
  also 0).
* **base1, held out**: with the likeliest action two runs of three never
  finish it, and one does 81%. Eight episodes of a run that scores 0 were
  looked at: all eight stop at the same crawl-space near the start, going
  to and fro. The training maps have no such place. Drawing actions gets
  through it, which is why the drawn figures are higher and steadier there
  (66% ± 18 after PPO). So a student does carry over to a map it has not
  seen, but whether its likeliest action does hangs on one spot and on the
  seed. **With three seeds this spread is the finding**; one seed would
  have reported 0% or 81%.
* On the maps trained on, the likeliest action does better than drawing; on
  the held-out map it is the other way round.

Settings. Cloning: 900 teacher episodes (about 1.05 M steps, of which the
teacher finished 65%), 12 passes. DAgger: 8 rounds of 300 episodes, the
teacher's share of steps 0.5, 0.3, 0.1, then 0; 6 passes over everything
each round; 2.5 M steps by the end. PPO: 3.0 M steps, 28 environments,
episodes of up to 4,500 steps, the reward of report 5; the value head alone
for the first 200,000 steps; the teacher's penalty from 0.5 down to 0.1 by
1.5 M steps and **kept at 0.1** (report 5 let it go to nothing); every
half million steps the policy is played 56 episodes with its likeliest
action on its own seeds, and **the weights kept are those that did best
there** (at 3.0 M, 2.5 M and 1.5 M steps in the three runs). One run takes
about 75 minutes: 4 recording, 1.5 cloning, 25 DAgger, 30 PPO, the rest
evaluating. Training and evaluation take the card in turn.

Changed since report 5, so these figures do not sit beside its figures:
the teacher (above); the observation (the ground about the feet; the
shared block is version 6 and older weights do not load); PPO as just
said. The base1 students of reports 4 and 5 were not retrained.

One run with the first of these settings, before the ground was added to
the observation and with fact3 as a third map (one seed, 100 episodes a
map, likeliest action): cloned 21% on base3, DAgger 37%, and PPO, with the
penalty let go to nothing and the last weights kept, 0%: it had fallen
apart on base3 by the end. That is why the penalty is now kept and the
best weights chosen.

Demos, in `~/q2pt-rl/demos/`, each checked with `dm2check.py`, none
watched: `teacher_base3.dm2` (88.6 s), `teacher_mintro.dm2` (241.4 s),
`student_ppo_base3.dm2` (a PPO student, likeliest action, 73.0 s) and
`student_ppo_base1_heldout.dm2` (the run that scores 81%, on the map it
never trained on, 165.6 s, 17 of 17 monsters).

## Every single-player map

28 episodes a map, seeds 0-27, 6,000 steps. "Test": whether a player with
no sense fails to reach an exit (above).

| map | a test | exit reached | killed | out of time | the commonest failure |
| --- | --- | --- | --- | --- | --- |
| base1 | yes | 27 of 28 | 1 | 0 | killed by a shotgun guard (1) |
| base2 | no | 0 of 28 | 1 | 27 | standing still, with no plan (27) |
| base3 | yes | 25 of 28 | 3 | 0 | killed: drowned (2) |
| train | no | 25 of 28 | 0 | 3 | standing still, with no plan (3) |
| bunk1 | yes | 0 of 28 | 2 | 26 | standing still, with no plan (26) |
| ware1 | no | 28 of 28 | 0 | 0 |  |
| ware2 | no | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| jail1 | yes | 0 of 28 | 27 | 1 | killed by a gunner (20) |
| jail2 | no | 0 of 28 | 28 | 0 | killed by a tank (15) |
| jail3 | no | 0 of 28 | 27 | 1 | killed by a gunner (19) |
| jail4 | no | 28 of 28 | 0 | 0 |  |
| jail5 | no | 28 of 28 | 0 | 0 |  |
| security | no | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| mintro | yes | 16 of 28 | 12 | 0 | killed by a berserker (6) |
| mine1 | no | 28 of 28 | 0 | 0 |  |
| mine2 | no | 28 of 28 | 0 | 0 |  |
| mine3 | no | 28 of 28 | 0 | 0 |  |
| mine4 | no | 24 of 28 | 4 | 0 | killed by a gunner (4) |
| fact1 | yes | 0 of 28 | 26 | 2 | killed by a gunner (22) |
| fact2 | no | 16 of 28 | 12 | 0 | killed by a gunner (12) |
| fact3 | no | 28 of 28 | 0 | 0 |  |
| power1 | yes | 0 of 28 | 26 | 2 | killed by a supertank (19) |
| power2 | no | 28 of 28 | 0 | 0 |  |
| cool1 | no | 28 of 28 | 0 | 0 |  |
| waste1 | no | 28 of 28 | 0 | 0 |  |
| waste2 | no | 28 of 28 | 0 | 0 |  |
| waste3 | no | 28 of 28 | 0 | 0 |  |
| biggun | yes | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| hangar1 | yes | 2 of 28 | 19 | 7 | killed by a enforcer (9) |
| hangar2 | no | 0 of 28 | 28 | 0 | killed by a gunner (27) |
| lab | no | 28 of 28 | 0 | 0 |  |
| command | no | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| strike | no | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| space | yes | 0 of 28 | 28 | 0 | killed: a trigger that hurts (28) |
| city1 | yes | 0 of 28 | 28 | 0 | killed by a gladiator (13) |
| city2 | no | 0 of 28 | 11 | 17 | standing still, with no plan (17) |
| city3 | no | 0 of 28 | 27 | 1 | killed by a iron maiden (16) |
| boss1 | yes | 0 of 28 | 0 | 28 | standing still, with no plan (28) |
| boss2 | yes | 0 of 28 | 28 | 0 | killed by a jorg (28) |

## Tests

`rl/tests`: 7 tests pass. CI on pull request 64 (Linux, Windows x64 and
x86, licences) passed on the commit it was opened with; it has not run on
the commits since, which are not pushed yet as this is written.

## Not done, not tested

* Five more maps: two.
* A map with a laser across the way, finished.
* Maps the student has not seen that are themselves new to the teacher's
  rules: the held-out map is base1, which the teacher's rules were first
  written on.
* PPO from random weights on these maps; the unguided student.
* Skill 0, 2, 3. Playing a unit across its maps.
* Windows was built by CI at the first push only.

## Disk

`~/q2pt-rl/`: 27 GB of the 500 GB allowed. 19 GB of datasets (three runs),
2.7 GB of datasets from the run before the observation changed, 5.5 GB of
Python environment, 157 MB of weights, 81 MB of navigation graphs, 1.2 MB
of demos. The datasets of reports 4 and 5 are still there and no longer
fit the observation.
