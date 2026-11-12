# Report 3: the teacher plays base1 to the exit

2026-10-10. Branch `rl-agent`. Machine as in report 0. Step length 100 ms,
skill 1 (normal), single-player rules.

## Result

**base1: 96 of 100 seeded episodes reach the exit** (seeds 0-99, 6,000 steps
allowed). The target was 90. Over seeds 0-199 it is 185 of 200 (92%).

Episodes that reach the exit take 122 s of game time (median), kill 15.9 of
the 17 monsters and end with 89 health.

The four failures of the 100, classed:

| cause | episodes | seeds |
| --- | --- | --- |
| killed by a shotgun guard | 4 | 37, 42, 73, 96 |

The 15 failures of the 200:

| cause | episodes |
| --- | --- |
| killed by a shotgun guard | 13 |
| killed by an enforcer | 1 |
| killed by a barrel set off beside it | 1 |

None runs out of time. `rl/tools/play.py base1` repeats the measurement in
about 20 s.

## What the teacher does on base1

Nothing in it is written for base1. From the map's entities it finds that
the exit is a trigger at the bottom of a lift, that the lift is a door with
a name, and that a button targets that name; it walks to the button, presses
it by walking into it, rides down and ends the map. On the way it turns
aside for the shotgun, the machinegun, armour, health and ammunition, and
shoots what it sees.

## What is scripted

* `game/g_rl_plan.c`: the plan, worked back from the exit through whatever
  opens the way (buttons to touch or shoot, triggers, keys, monsters whose
  death fires something), from the entities as they are now.
* `game/g_rl_fight.c`: target, aim with lead, weapon by distance.
* `game/g_rl_teach.c`: following the route, dodging, picking up, and the
  rules found necessary at 100 ms steps (below).

The plan and the costs are worked out again every 16 steps from the state of
the world. What the teacher remembers between steps is about the player:
the node it last stood at and how fast its view was turning.

## What had to be added to get from 36% to 96%

In the order found, each with what it fixed:

| change | exit reached |
| --- | --- |
| first version of planner and combat | 10 of 28 |
| shoot only at what a shot can reach (not through gratings); take only items that lie at a node | 29 of 56 |
| a node is "crouched" only where there is no room to stand (the graph had crouch nodes in open floor, and routes zigzagged between them) | 46 of 56 |
| keep the aim on one monster; wait for a monster that is coming; keep clear of barrels | 50 of 56 |
| follow a jump or a drop on the heading it was found on, from its node | 88 of 100 |
| step away from barrels when beside one | 173 of 200 |
| go on at a slant when a corner is caught by a hair | 185 of 200 |

The last is the general form of what report 2 found: at 100 ms steps the
player stops dead against a corner it overlaps by an eighth of a unit, and a
full step aside brings it back to the same eighth. Going on 12 to 45 degrees
to one side clears it. Before this, 4% of episodes went round in a circle at
one doorway near the exit.

## Every single-player map, as it stands

28 episodes per map, 6,000 steps, seeds 0-27. An exit counts if it leads to
a map later in the game's order. Each map is played alone, from a fresh
start with the blaster only. All 39 maps took 2 min 21 s on 28 threads.

| map | exit reached | killed | out of time | the commonest failure |
| --- | --- | --- | --- | --- |
| base1 | 28 of 28 | 0 | 0 |  |
| base2 | 6 of 28 | 9 | 13 | standing still with no plan (13) |
| base3 | 12 of 28 | 12 | 4 | killed by a parasite (4) |
| train | 0 of 28 | 0 | 28 | standing still, fighting (28) |
| bunk1 | 0 of 28 | 3 | 25 | standing still with no plan (25) |
| ware1 | 1 of 28 | 27 | 0 | killed by a gunner (12) |
| ware2 | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| jail1 | 0 of 28 | 4 | 24 | on the move, making for an item by a jump (16) |
| jail2 | 0 of 28 | 28 | 0 | killed by a gunner (17) |
| jail3 | 0 of 28 | 16 | 12 | standing still, fighting (11) |
| jail4 | 0 of 28 | 28 | 0 | killed by a tank (25) |
| jail5 | 26 of 28 | 2 | 0 | killed by a gunner (2) |
| security | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| mintro | 0 of 28 | 28 | 0 | killed: lava (27) |
| mine1 | 28 of 28 | 0 | 0 |  |
| mine2 | 28 of 28 | 0 | 0 |  |
| mine3 | 28 of 28 | 0 | 0 |  |
| mine4 | 3 of 28 | 25 | 0 | killed by a gunner (13) |
| fact1 | 0 of 28 | 28 | 0 | killed by a laser (27) |
| fact2 | 0 of 28 | 28 | 0 | killed: lava (17) |
| fact3 | 28 of 28 | 0 | 0 |  |
| power1 | 0 of 28 | 28 | 0 | killed by a supertank (20) |
| power2 | 28 of 28 | 0 | 0 |  |
| cool1 | 0 of 28 | 25 | 3 | killed by a gunner (19) |
| waste1 | 0 of 28 | 24 | 4 | killed by a gunner (24) |
| waste2 | 0 of 28 | 28 | 0 | killed by a gladiator (20) |
| waste3 | 28 of 28 | 0 | 0 |  |
| biggun | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| hangar1 | 0 of 28 | 27 | 1 | killed by a gunner (9) |
| hangar2 | 0 of 28 | 28 | 0 | killed by a gunner (21) |
| lab | 28 of 28 | 0 | 0 |  |
| command | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| strike | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| space | 0 of 28 | 28 | 0 | killed by a trigger that hurts (28) |
| city1 | 0 of 28 | 28 | 0 | killed by a gladiator (14) |
| city2 | 0 of 28 | 1 | 27 | standing still, fighting (20) |
| city3 | 0 of 28 | 26 | 2 | killed by a machinegun guard (10) |
| boss1 | 0 of 28 | 0 | 28 | standing still with no plan (28) |
| boss2 | 0 of 28 | 28 | 0 | killed by jorg (28) |

10 of 39 maps are at 26 of 28 or better, but **only base1 of those is a map
played through**. On the others the exit that leads on is at the start, or
nearly, because in a hub the player arrives beside the way to the next map
and the game expects it to go off for keys first:

| map | time to the exit (median) | monsters killed |
| --- | --- | --- |
| base1 | 122 s | 15.9 of 17 |
| fact3 | 31 s | 0 of 0 |
| jail5 | 20 s | 2 of 56 |
| mine3 | 5 s | 0 of 20 |
| power2 | 3 s | 0 of 43 |
| mine1, mine2, waste3, lab | 1 s | 0 |

So the count that means anything is 1 map of 39, with base2 (6 of 28) and
base3 (12 of 28) partly done. 27 maps are at 3 of 28 or worse. Nothing but
base1 has been worked on. The failures fall into four kinds:

* **No plan** (9 maps): the way out hangs on something the planner does not
  know: a train, a teleporter, a wall to be blown up, a lift worked by
  `trigger_elevator`, or, in a hub, a key or power cube that lies in
  another map and cannot be had in an episode that plays one map alone.
  Which it is has not been looked into map by map.
* **Outgunned** (about 14 maps): gunners, gladiators, tanks and bosses
  against a player that starts every map with the blaster only, as it would
  not in the game. Fighting that passes on base1 is not good enough here.
* **The floor** (mintro, fact2, space, fact1): lava, a trigger that hurts,
  lasers. The graph leaves out lava and hurting triggers but the teacher
  still ends in them, pushed or dodging; lasers it does not know at all.
* **Standing and fighting** (train, jail3, city2): a monster it can see and
  aim at but does not kill.

Reaching "an exit that leads on" is too easy a test for a hub map. A fair
one needs the unit played across its maps with the inventory kept, which is
the stretch goal.

## Demos

`~/q2pt-rl/demos/teacher_base1.dm2`: the teacher through base1, seed 0:
144.6 s, 17 of 17 monsters, 96 health at the exit. 1,446 frames, 208 kB.
`~/q2pt-rl/demos/explore_base1.dm2`: the explorer, recorded again.

**A fault in the demos of report 2 was found and fixed here.** The reliable
messages were written as blocks of their own. A server playing a demo deals
out one block per frame, so those demos played a fifth too long, the picture
standing still for a tenth of a second about twice a second. It did not
show in stills; it showed when a 144.6 s demo rendered as 175 s of blocks.
Each frame's block now holds the reliable messages, the frame and the
effects together, and `dm2check.py` fails a file with frameless blocks. The
teacher demo was rendered whole with `pt_render` at 10 frames a second:
1,447 frames, 144.7 s, as recorded. No frame was dropped for size.

## Tests

`rl/tests`: 7 tests, 4.5 s, pass. New: the teacher playing base1 with the
monsters in gives the same run when repeated and in a new process.

## Steps per second

One server on thread 10, the teacher playing base1: 3,660 steps/s (random
actions: 6,880; the explorer: 6,140). The planner's route costs are the
difference.

## Explorer, after these changes

All 39 maps with the monsters out, as in report 2: 5,924 of 6,425 goals
reached, 92.2% (report 2: 94.4%). The changes made for base1's play cost
the explorer two points elsewhere; which change has not been found.

## Not done, not tested

* Any map but base1 has only been measured, not worked on.
* Skill 0, 2 and 3.
* Windows was not built. Nothing is pushed, so CI has not run.
* The teacher's labels in states it did not drive into.

## Disk

`~/q2pt-rl/`: 81 MB of navigation graphs, 0.4 MB of demos, 86 MB of Python
environment. 0.17 GB of the 500 GB allowed.

## Rebase

Main has not moved (8eb4fa7): nothing to rebase.
