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
