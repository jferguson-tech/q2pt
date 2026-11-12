# Report 2: navigation and the explorer

2026-10-10. Branch `rl-agent`. Machine as in report 0. Step length 100 ms.

## What there is now

* `game/g_rl_nav.c`: a navigation graph of each map, grown by moving a ghost
  of the player through the engine's `Pmove` in 100 ms steps (walk, jump,
  crouch, swim, ladder), with doors and lifts tried at both ends of their
  travel. Kept per map in `~/q2pt-rl/nav/`.
* `game/g_rl_teach.c`: the teacher, so far an explorer. It draws a node that
  can be reached now from the seed, walks there by the cheapest route, and
  draws the next. It gives up on a goal that takes four times the route's
  length plus five seconds. Its turning is limited to 30 degrees a step and
  to a change of 12 degrees a step from one step to the next.
* The teacher's action is in the block after every step, whoever acted, and
  so are the guide (direction and distance of the next node) and the
  progress made along the route.
* `rl/tools/explore.py` measures the table below; `rl/q2env/nav.py` reads a
  graph file.

## Coverage, every single-player map

8 episodes of 3,000 steps (5 minutes of game time) per map, the teacher
driving, seeds 0-7. **The monsters were taken out** (`FLAG_NOMONSTERS`), so
this measures the finding of the way and nothing else: the explorer does not
fight yet and with monsters it is dead within a few hundred steps on base1.
All 39 maps, graphs built from nothing, took 33 s on 28 threads.

"Nodes visited" is the share of the graph's nodes the player was at in any
of the 8 episodes. "Goals" are those the explorer set itself.

| map | nodes | walk | jump | duck | swim | climb | ride | goals reached | given up | nodes visited |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base1 | 6829 | 40796 | 12177 | 4341 | 9528 | 49 | 146 | 143 of 147 (97%) | 4 | 40% |
| base2 | 4919 | 30596 | 5128 | 2126 | 2478 | 0 | 210 | 106 of 109 (97%) | 3 | 41% |
| base3 | 9803 | 47816 | 31723 | 3444 | 38628 | 0 | 250 | 77 of 78 (99%) | 1 | 23% |
| train | 10759 | 37865 | 62478 | 3356 | 88543 | 0 | 290 | 247 of 255 (97%) | 8 | 21% |
| bunk1 | 10397 | 59517 | 29403 | 3266 | 28491 | 0 | 1092 | 1382 of 1386 (100%) | 4 | 6% |
| ware1 | 9855 | 68151 | 9316 | 1989 | 0 | 125 | 738 | 618 of 640 (97%) | 22 | 15% |
| ware2 | 5017 | 34993 | 4019 | 825 | 0 | 0 | 316 | 496 of 496 (100%) | 0 | 13% |
| jail1 | 18663 | 56970 | 120423 | 5683 | 177348 | 26 | 494 | 18 of 18 (100%) | 0 | 8% |
| jail2 | 12484 | 81424 | 20254 | 5062 | 11547 | 0 | 534 | 238 of 243 (98%) | 5 | 16% |
| jail3 | 7405 | 49531 | 7935 | 3488 | 0 | 0 | 242 | 52 of 52 (100%) | 0 | 18% |
| jail4 | 10647 | 61967 | 30341 | 3520 | 30802 | 0 | 252 | 167 of 183 (91%) | 16 | 19% |
| jail5 | 8166 | 47788 | 20868 | 2249 | 20430 | 20 | 128 | 144 of 153 (94%) | 9 | 25% |
| security | 5328 | 35612 | 2424 | 532 | 0 | 0 | 202 | 40 of 70 (57%) | 30 | 19% |
| mintro | 5259 | 30952 | 13355 | 1547 | 13061 | 16 | 98 | 27 of 28 (96%) | 1 | 11% |
| mine1 | 55 | 404 | 42 | 4 | 0 | 0 | 0 | 0 of 0 (0%) | 0 | 2% |
| mine2 | 186 | 1304 | 69 | 22 | 0 | 0 | 0 | 110 of 110 (100%) | 0 | 85% |
| mine3 | 4951 | 13505 | 36950 | 784 | 52065 | 52 | 72 | 277 of 296 (94%) | 19 | 57% |
| mine4 | 1336 | 10603 | 1281 | 199 | 0 | 0 | 74 | 105 of 109 (96%) | 4 | 66% |
| fact1 | 5798 | 39422 | 9141 | 1888 | 6569 | 46 | 152 | 5 of 7 (71%) | 2 | 4% |
| fact2 | 1331 | 9815 | 900 | 143 | 0 | 0 | 178 | 7 of 7 (100%) | 0 | 10% |
| fact3 | 2112 | 14982 | 1774 | 511 | 0 | 0 | 24 | 30 of 30 (100%) | 0 | 33% |
| power1 | 5700 | 39691 | 4528 | 1283 | 0 | 26 | 118 | 231 of 242 (95%) | 11 | 32% |
| power2 | 743 | 5406 | 1268 | 200 | 0 | 0 | 0 | 119 of 152 (78%) | 33 | 54% |
| cool1 | 37200 | 158128 | 192561 | 3393 | 257407 | 25 | 277 | 284 of 301 (94%) | 17 | 4% |
| waste1 | 553 | 3733 | 585 | 146 | 0 | 0 | 0 | 448 of 448 (100%) | 0 | 90% |
| waste2 | 3499 | 25123 | 3865 | 377 | 0 | 0 | 38 | 108 of 125 (86%) | 17 | 44% |
| waste3 | 417 | 2759 | 643 | 132 | 0 | 0 | 0 | 15 of 15 (100%) | 0 | 31% |
| biggun | 3622 | 25019 | 2657 | 752 | 0 | 0 | 292 | 190 of 275 (69%) | 85 | 21% |
| hangar1 | 7119 | 21473 | 44938 | 2559 | 65064 | 15 | 206 | 9 of 9 (100%) | 0 | 4% |
| hangar2 | 18383 | 110695 | 30410 | 4594 | 21783 | 44 | 1490 | 10 of 13 (77%) | 3 | 2% |
| lab | 9120 | 59836 | 10820 | 2883 | 0 | 0 | 266 | 260 of 269 (97%) | 9 | 16% |
| command | 8740 | 36530 | 41001 | 3287 | 54751 | 0 | 142 | 122 of 131 (93%) | 9 | 32% |
| strike | 21412 | 21809 | 208809 | 3390 | 318192 | 0 | 0 | 135 of 136 (99%) | 1 | 26% |
| space | 6799 | 45918 | 3026 | 1666 | 0 | 0 | 158 | 56 of 75 (75%) | 19 | 20% |
| city1 | 25101 | 167239 | 44307 | 5713 | 34004 | 10 | 320 | 22 of 29 (76%) | 7 | 2% |
| city2 | 4763 | 32188 | 5556 | 1608 | 0 | 82 | 236 | 178 of 205 (87%) | 27 | 17% |
| city3 | 1454 | 9956 | 1718 | 454 | 0 | 30 | 110 | 29 of 49 (59%) | 20 | 24% |
| boss1 | 666 | 4625 | 872 | 111 | 0 | 0 | 0 | 183 of 191 (96%) | 8 | 76% |
| boss2 | 3792 | 26259 | 1756 | 1107 | 0 | 0 | 136 | 85 of 93 (91%) | 8 | 42% |

All maps: 6,773 of 7,175 goals reached, 94.4%. By map, the median is 96%;
28 of 39 maps are at 90% or more; four are under 70% (security, biggun,
city3, and mine1, where nothing can be reached from the start).

Nodes visited is low on most maps (median 21%, from 2% to 90%) for two
reasons, neither of them a failure to walk:

* The graph holds everything that could be reached if every door and lift
  were where it needs to be. The explorer only goes where it can get as
  they stand, and does not press buttons, fetch keys or call lifts yet. On
  mine1, bunk1, fact1, city1, hangar1 and cool1 that leaves it a small part
  of the map.
* 24,000 steps is not long enough to visit a graph of 10,000 nodes. Water
  makes graphs large: a swimmer has nodes at every depth.

What the 402 goals given up were doing when the time ran out:

| what | goals |
| --- | --- |
| walking, going to and fro between the same nodes | 85 |
| walking, on the move | 55 |
| nowhere to go from where it stood | 47 |
| walking, standing still | 42 |
| walking at a door or lift, standing still | 38 |
| jumping, standing still | 26 |
| walking at a door or lift, on the move | 23 |
| jumping, on the move | 19 |
| ladders | 23 |
| swimming | 10 |
| other | 34 |

40 of the 85 "to and fro" are one staircase on biggun.

## What was learned about moving in 100 ms steps

* **Head-on into a wall there is no slide.** `Pmove` stops the player dead
  when the wall turns its velocity back on itself, which at this step length
  is anything within about 6 degrees of square. A player one unit to the
  side of an opening as wide as itself does not slip in. The teacher steps
  to the side when something is dead ahead.
* **A step cannot be climbed under a low roof.** `Pmove` steps up by trying
  the move 18 units higher, and under a 72 unit ceiling a standing player
  has no room for that, even for a 6 unit door sill. Crouched or with a hop
  it gets over. The teacher hops when it is not moving and only something
  below the shins is in the way.
* **A move that works from one spot may not work a few units away.** The
  first graphs had links that cleared a ledge only from the exact place they
  were tried from, and the explorer went round in circles on them. Links
  are now tried from three more starting places, which removed 4,500 of
  70,800 on base1.
* The feet can go only 8 ways about the view. The view therefore settles on
  a heading a whole number of 45 degree turns from the way to go, so that
  one of the 8 is exact.

## Demo

`~/q2pt-rl/demos/explore_base1.dm2`: 180 s of the explorer on base1, seed 3,
monsters taken out, 12 goals reached, none given up. 1,800 frames, 200 kB,
no frame dropped.
`~/q2pt-rl/demos/explore_base1_monsters.dm2`: the same with the monsters in
and told to ignore the player (`FLAG_NOTARGET`). They stand in corridors and
the explorer reaches 1 goal and gives up 2.

Both pass `dm2check.py`. 24 s of the first were rendered with `pt_render` in
the full client here (RTX renderer, 10 frames a second): the view moves
forward smoothly from frame to frame and the client reports no error. Looked
at as stills only.

**Corrected in report 3**: these two files as first written put the
reliable messages in blocks of their own, about one for every five frames.
A server playing a demo deals out one block per frame, so they played a
fifth too long, with the picture held still a tenth of a second at a time.
That did not show in stills. Both were recorded again after the fix; a copy
taken before then should be thrown away.

**To play one on the workstation**: copy it into `baseq2/demos/` and type
`demomap explore_base1.dm2`, or `pt_render explore_base1`.

## Tests

`rl/tests`: 6 tests, 3.3 s, all pass. New: the teacher driving gives the
same run when repeated, and the same whether the graph was built for that
run or read from its file. (Building moves doors, lifts and monsters about,
so after a build the episode is started over from a clean map.)

## Steps per second

One server on thread 10 with the teacher driving base1: 6,140 steps/s with
no monsters, 4,260 with them (it dies early, so resets weigh more). Random
actions were 6,880 in report 1: the teacher costs about a tenth.

## Not done, not tested

* Buttons, keys, lifts that must be called, doors that are lifts, trains,
  and walls that are shot away: the next milestone's planner.
* The explorer with monsters shooting at it: it has no combat.
* Windows was not built. Nothing is pushed, so CI has not run.
* Whether the teacher's labels are sound in states it did not drive into
  has not been measured: that comes with DAgger.

## Disk

`~/q2pt-rl/`: 66 MB of navigation graphs (39 maps), 0.7 MB of demos, 86 MB
of Python environment. 0.15 GB of the 500 GB allowed.

## Rebase

Main has not moved (8eb4fa7): nothing to rebase.
