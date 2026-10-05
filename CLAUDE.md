# Working rules for this repository

## Branches and pull requests
- Never commit or push to main directly.
- Before starting any feature or fix (RTX tracer, ghosting, particles, ...):
  1. git checkout main && git pull
  2. git checkout -b <short-name>   (for example rtx-basic-trace)
  3. Push the branch and open a pull request early; keep pushing to it.
- One feature per branch. Do not stack a branch on another unmerged branch.
- Merge only after CI is green and the user approves: squash merge, delete
  the branch, then check the CI run on main.
- GitHub does not enforce this while the repository is private, so these
  rules are the only safeguard.

## Commits and text
- No "Co-Authored-By", "Generated with" or any AI attribution in commits,
  pull requests or release notes.
- Plain, factual messages. Say what was and was not tested.

## Licensing
- pt/ is MIT and engine independent: nothing in it may include or copy from
  the Quake 2 sources, and every file keeps its SPDX MIT header.
- Everything else, including ref_pt/, is GPL v2.
- New RTX code that is engine independent goes in pt/rtx/; anything that
  touches Quake 2 types goes in ref_pt/.

## Never commit
- Game data or build output: nothing from run/ or build/, no .pak, .wal,
  .pcx, .bsp, .md2, .dm2, .wav, .cin, .exe, .dll, .pdb.

## Releases
- No release, tag or binary download unless the user asks.
