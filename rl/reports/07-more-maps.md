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

STUDENTS

## Every single-player map

28 episodes a map, seeds 0-27, 6,000 steps. "Test": whether a player with
no sense fails to reach an exit (above).

TABLE

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

DISK
