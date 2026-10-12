# Report 8: the base unit, a leg at a time

2026-10-11. Branch `rl-agent`. Machine as in report 0. Step length 100 ms,
skill 1 (normal), single-player rules.

## What an episode is now

Until now an episode was a map played alone: the plain start, the blaster,
and any exit that did not lead to an earlier map. Report 7 found that this
tests little on most maps, because in a hub the player stands beside the
door it would have come in by.

An episode is now a **leg** of a unit:

* it starts at the spawn point that the exit of the map before names
  (`base2$base1`: base2, where the door from base1 lets the player in);
* it starts with what the teacher had when it left that map: health,
  weapons, ammunition, keys, and the unit's cross-map switches. One row is
  kept for every teacher episode that reached the exit, and an episode
  takes the row its seed picks;
* it succeeds only at the exit that leads on. Any other exit ends it as a
  failure ("left by another exit").

The legs of the base unit, in order:

| leg | starts | ends at the exit to |
| --- | --- | --- |
| base1 | a new game | base2 |
| base2@base1 | in base2, from base1 | base3 |
| base3@base2a | in base3, from base2 | base2, by its other door |
| base2@base3b | in base2, from base3 | bunk1, the next unit |

base3 also has a door to train and back. Train has that one door and
nothing the unit needs, so it is not a leg.

**Not carried: the state of the map.** The game saves a map on leaving and
loads it on coming back. Here base2 entered from base3 is base2 as new, its
monsters alive again. That makes the last leg harder than it is in the game.

## The random walker under the new definition

`rl/tools/walker.py` now puts its two players (one walks forward, one acts
at random; 14 episodes each, 3,000 steps) into every map at every place the
game brings the player in, and reports which exit they left by.

84 arrivals in the 39 maps:

| what the two players do | arrivals |
| --- | --- |
| leave by no exit at all | 17 |
| leave by the exit beside them, back to the map they came from | 67 |
| reach any other exit | 0 |

So **an arrival is a real test whenever the way on is not the door the
player came in by.** That is every leg of the base unit: on all four the
walker never reaches the exit that leads on, and on two of them it walks
back out of the door behind it within 6 seconds, which is now a failure.

The exception is a map with a single exit, entered and left by the same
door: train, ware1, jail5, fact2, fact3, lab, space, mine4. There the
walker "succeeds" by walking back out, and no definition by exit can test
the visit, which is for something done inside. For the units after the
first I have not worked out which exit leads on from each arrival, so the
table says only where the walker went. The whole table is in
`~/q2pt-rl/logs/walker_unit.md`.

## The teacher

200 seeded episodes a leg (seeds 0-199), 6,000 steps allowed, each from the
teacher's own carry out of the leg before:

| leg | reaches the exit | killed | out of time | takes (median) | leaves with |
| --- | --- | --- | --- | --- | --- |
| base1 | 196 of 200 (98%) | 4 | 0 | 101 s | 99 health |
| base2@base1 | 192 of 200 (96%) | 2 | 6 | 85 s | 100 health |
| base3@base2a | 168 of 200 (84%) | 26 | 6 | 226 s | 95 health |
| base2@base3b | 167 of 200 (84%) | 23 | 10 | 90 s | 83 health |

**The unit as one run**, each leg from exactly what that run carried out of
the last (`rl/tools/unit.py`, 100 runs): 97 got through base1, 93 of those
through base2, 85 through base3 and **74 through the whole unit**.

Episodes that run out of time: 22 of 800 (2.8%). Most stand somewhere the
graph has no way out of: a pit under base2's last yard, the top of a lift.
Those I looked at were all of that kind; I did not look at all 22.

What it took, beyond the new start and end:

* **base2's exit to base3 hangs in the air**, in a pit whose floor is a
  pair of doors. No node of the graph is inside it, so the planner had no
  place to go. A fall through a trigger now counts as touching it.
* **The way from base3's door to base2's last yard is walled off** until a
  trigger removes the wall. The graph now treats a wall that a `killtarget`
  removes as it treats a door, and the planner finds what removes it.
* **The graph was grown from one start only.** It is now grown from every
  spawn point, or the part of base2 that is entered from base3 would not be
  in it.
* **A freed entity's number is reused.** After the wall was removed, a
  blaster bolt or a dropped clip took its number and was taken for the wall
  still standing, which shut the way for good. 15 of 100 episodes on the
  last leg stood with no plan because of it.
* **An item that cannot be got** (a jump that keeps failing) held the
  teacher until time ran out in 17 of 100 on the last leg. An item is now
  given up after three times the walk it should take.
* **Aim.** Two faults, found on the gunners of base2's last yard, which
  killed 37 of 100:
  * the machinegun climbs a degree and a half a shot in a single-player
    game, and after eight shots every one went over the gunner's head. The
    teacher now holds it down;
  * the aim was led by the player's present velocity. Sidestepping to and
    fro, that is the wrong way round every step or two, and the teacher
    hit almost nothing while it dodged. It now aims for where the step it
    is about to take puts it.

  The second mattered most: with it the last leg went from 56 to 77 of 100
  and base3 from 38 to 88 (same seeds, before the carry files were
  renewed).

One thing tried and dropped: walking on unfought past a gunner when there
was no room to sidestep. It took the last leg from 56 to 35.

@@STUDENTS@@

## What is not comparable with earlier reports

* Every figure before this report is for a map played alone from its plain
  start with the blaster, any exit counting. None of those tables can be
  set beside the ones here: the start, the inventory and what counts as
  success have all changed. base3 in report 7 ended at the door to train;
  here it ends at the far door to base2.
* The block the game shares with Python is version 7 (the leg's start and
  end were added). What the student perceives did not change, so the
  report 7 weights still load; they were trained for other episodes.
* The teacher changed (aim, the machinegun, items given up), so teacher
  data recorded before today is not what it would do now.

## Housekeeping

* GPU: training and evaluation took the card in turn; nothing ran on it
  beside them.
* Disk: `~/q2pt-rl/` is @@DISK@@ of the 500 GB allowed.
* Docker: the two containers were not touched.
* CI on the pull request (Linux, Windows x64, Windows x86, licences):
  @@CI@@.
* Rebase on main: @@REBASE@@.
* Demos, in `~/q2pt-rl/demos/unit_base/`: one teacher run through the whole
  unit, a file a leg@@DEMOS@@. All pass `dm2check.py`; nobody has watched
  them.
