# Spike results and PC facts

## Owner's setup
- GTA V Enhanced, Epic Games Store, game version 1.0.1158.16_v7. ScriptHookV works (a trainer runs on it).
- Assetto Corsa with Content Manager and CSP 2.11.

## Spike A — AC input/FFB in the background
**Passed (2026-11-17).** With GTA focused, AC kept reading the wheel and pedals, the car moved, and FFB
stayed on. No workaround needed.

## Spike B — generated downtown tile in AC
**Passed (2026-11-17), with --visible --double-sided.** Car lands on the road and drives; buildings are walls;
scale feels the same as GTA. Some slight gaps (bumps, not holes) — location still to be pinned down.
Findings on the way:
- hi@ collision files have no road surface; cars use the normal (mover) files. surfacegen now prefers those.
- Real CodeWalker XML separates numbers with spaces; kerbs are `RUMBLE_STRIP`; materials.dat raw export can be a folder.
- Start used: GTA (243.73, -948.15, 28.87), heading 158.99 — ground 0.62 m below.
- Downtown run: 72 files, 1,193,120 triangles (WALL 670,765, ROAD 481,098, GRAVEL 22,764, GRASS 17,948, SAND 545).
- Still to check: whether the invisible, single-sided build (no flags) also holds the car.

## Spike C — puppet follows AC (runway test), GTA FPS with AC running
_not run yet_

## Stage 1 in GTA (2026-11-17)
**Working.** AC (Kunos Z370) drives a GTA ZR350 through downtown: follows smoothly, height matched, off-map warning and
AC resets carry over. Fixes found on the way:
- CSP 2.11 lays shared-memory fields out alphabetically: keys are now prefixed `_00_magic`, `_01_version`, ...
- CSP gives no tyre contact points: AC's ride height is measured against GTA's ground at F8 (retried until loaded).
- GTA-only props (trees, poles) wrecked/flipped the GTA car: damage-proof + `frozen=1` by default.
- Walls: AC treats every surfaces.ini entry as drivable ground. `1WALL` meshes must NOT be listed there to act as
  barriers; also single-sided with real normals (coincident back faces cancelled the collision).
- Kerbs became walls by slope alone: steep faces under 35 cm stay drivable (`--step-height`).
