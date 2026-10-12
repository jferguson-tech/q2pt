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

## The students

Trained on the three legs that follow base1, with base1 held out. Every
episode starts from the teacher's carry. Each run is 900 teacher episodes
cloned, 8 rounds of DAgger and 3.0 M steps of PPO from the DAgger student,
as in report 7. Three whole runs from different seeds; 100 evaluation
episodes a leg each, on seeds no training uses. Mean of the three runs ±
the spread between them, **playing the likeliest action**; in brackets,
drawing actions:

| player | base2@base1 | base3@base2a | base2@base3b | base1, held out |
| --- | --- | --- | --- | --- |
| teacher | 95% | 86% | 84% | 100% |
| cloned | 30% ± 15 (11% ± 4) | 0% ± 0 (0% ± 0) | 0% ± 1 (1% ± 1) | 19% ± 32 (17% ± 7) |
| DAgger | 81% ± 9 (66% ± 7) | 20% ± 9 (1% ± 1) | 8% ± 2 (2% ± 2) | 77% ± 25 (55% ± 6) |
| PPO from DAgger | 86% ± 4 (84% ± 6) | 30% ± 9 (5% ± 3) | 53% ± 2 (15% ± 6) | 0% ± 1 (71% ± 5) |

On the legs trained on:

* Each stage adds. For the DAgger and PPO students the likeliest action
  does as well as drawing on the first leg and much better on the other
  two.
* The first leg is nearly the teacher's. **base3 and the last leg are far
  from it**: 30% against 86%, and 53% against 84%. How the PPO students'
  900 episodes ended:

  | leg | exit | died | out of time | another exit |
  | --- | --- | --- | --- | --- |
  | base2@base1 | 258 | 17 | 17 | 8 |
  | base3@base2a | 90 | 159 | 51 | 0 |
  | base2@base3b | 159 | 95 | 46 | 0 |

  In 56 episodes of one PPO student on base3 (seed 0 of the first run:
  see below), the deaths were by gunners
  (6), parasites (4), **lasers (4)**, and seven other things once or twice
  each. The teacher is never killed by base3's lasers.
* The wrong exit is not the trap for a trained student that it is for the
  walker: 8 of 900.

On base1, which no student saw:

* **The three PPO students reach its exit in 0, 0 and 1 of 100 episodes
  with the likeliest action**, and in 65, 73 and 74 when actions are drawn.
  All three stop at one low passage near the start, where the teacher
  crouches. There they give crouching a probability of 2 to 5 in 100:
  drawn, it comes up soon enough; as the likeliest action, never.
* The DAgger students they were made from reach the exit in 49, 86 and 97
  of 100 with the likeliest action. **PPO on the other three legs made play
  by the likeliest action on the unseen map worse, and play by drawn
  actions better** (55% to 71%).

The unit as one run (`tools/unit.py`, 50 runs each): no PPO student gets
through with the likeliest action, since none leaves base1 (0 of 50, each of the three).

### The likeliest action is all or nothing, and rounding can tip it

The first time these runs were made, the PPO students of seeds 0 and 1
scored 90 and 0 of 100 on base1 with the likeliest action. The second
student's same weights, evaluated on the processor instead of the card,
scored 98.

The card was working the convolutions out in TF32, PyTorch's default on
this hardware, which keeps ten bits of a number's fraction. Play by the
likeliest action has nothing random in it: where two actions score nearly
alike the rounding picks the other, and the player can then settle into a
loop that it never leaves. With TF32 off the card and the processor agree:
the same 28 episodes all reach the exit on both, in the same mean number
of steps to the tenth.

So:

* TF32 is now off for everything (`rl/q2rl/model.py`).
* PPO keeps the checkpoint that does best on its own validation, which is
  play by the likeliest action; so the three PPO stages were run again in
  full precision, and every figure by the likeliest action in the table
  above is in full precision. The cloned and DAgger weights are the first
  run's: their training does not depend on such play.
* The first run's PPO students did not all stop on base1 (90 and 0, or 90
  and 98 in full precision); the second run's all do. Whether a student
  gets through that passage with the likeliest action is two-valued and
  turns on very little. **Three seeds do not put a useful error bar on a
  figure like that**, and "0% ± 1" above should be read with the first
  run in mind.
* Report 7's held-out figures were made in TF32. Its three PPO students,
  evaluated again today in full precision, reach base1's exit in 0, 97 and
  92 of 100 where the report had 0, 81 and 12. (The teacher and the graph
  have changed since, which changes the guide the student is given, so
  this is not a clean repeat.) That report's "a matter of the seed" was
  partly this.

Two of the first run's PPO students, which did pass base1, were played
through the unit as one run before they were replaced: 2 and 6 of 50 got
through. Both lost most at base3, 9 of 40 and 9 of 32, which is lower than
the same students do from the teacher's carry (38% and 29%): they arrive
with less than the teacher does.

### A crash in the game

During the second PPO run a server died: `monster_use` read through a null
activator. A monster that dies with no enemy fires its targets with nobody
as the activator, and when one of those targets is a monster the original
code crashes. It is a fault of the game as released; one line now returns
instead. Seen once in about 15 million steps of students' play.


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

* GPU: training and evaluation took the card in turn. The exception: the
  three short checks that found the TF32 fault (28 episodes each) ran on
  the card while seed 2 trained, with their memory capped at 6% of it.
* Time: a seed now takes about 2 h 20 min (5 recording the teacher, 2
  cloning, 40 DAgger, 38 PPO, 50 evaluating); these maps make longer
  episodes than base1 did.
* Disk: `~/q2pt-rl/` is 45 GB of the 500 GB allowed.
* Docker: the two containers were not touched.
* CI on the pull request (Linux, Windows x64, Windows x86, licences):
  passed on 97083e2, which has all of the game-side changes but the one
  line in `monster_use`. The pushes after it are checked there.
* Rebase on main: main has not moved since the last one (086f2dd), so there was nothing to rebase onto and no conflict.
* Demos, in `~/q2pt-rl/demos/unit_base/`: one teacher run through the whole
  unit, a file a leg; and in `unit_base_student/` the seed 0 PPO student on
  each of the three legs it trained on, by the likeliest action, from the
  teacher's carry. All pass `dm2check.py`; nobody has watched
  them.
