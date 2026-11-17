# GTA V × Assetto Corsa Merge — Implementation Plan (Stages 1–2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive an Assetto Corsa–simulated car through the original GTA V Enhanced world (stage 1), with GTA collisions fed back into AC physics and FFB (stage 2).

**Architecture:** AC runs hidden on an invisible track generated from the owner's GTA collision files. A CSP car script publishes the car's state into shared memory; a ScriptHookV `.asi` in GTA reads it and drives a puppet GTA vehicle every frame. In stage 2 the `.asi` measures how GTA's physics deflected the puppet and writes impulses back; the CSP script applies them.

**Tech Stack:** C++17 (MSVC on Windows for the `.asi`, g++ + doctest on Linux for unit tests), ScriptHookV SDK (Enhanced), CSP Lua, Python 3.11 (surface generator, pytest), CodeWalker (XML export of `.ybn`).

**Spec:** `docs/superpowers/specs/2026-11-17-gta-ac-merge-design.md`

Paths below are relative to the new repo root (`gta-ac-merge/`). Stage 3 (police) and polish (AC car models in GTA, cockpit cam) get their own plan after stage 2.

## Global Constraints

- Single-player, offline only. GTA V **Enhanced**, ScriptHookV already installed and working (owner runs a trainer). Never run GTA Online with the `.asi` installed.
- No Rockstar asset (exported XML, generated track) is ever committed. `.gitignore` covers `work/`, `*.kn5`, `*.ybn.xml`.
- Coordinate transform: `AC = (gta.x, gta.z, -gta.y)`; inverse `GTA = (ac.x, -ac.z, ac.y)`. Metres both sides.
- AC car audio, GTA world audio; GTA puppet engine muted.
- Start area: downtown Los Santos tile. AC damage off.
- Windows-only steps are marked **[PC]** and verified by hand; everything else has tests that run on Linux.

## Review Focus

1. **AC closed, paused or session restarted mid-drive** → puppet freezes in place, GTA stays playable, link resumes by itself when AC is back. Pinned in Task 2 (`stale after 250 ms`) and Task 7 step 5.
2. **Torn read while AC writes** → never a half-old/half-new pose. Pinned in Task 2 (seqlock test).
3. **Teleport/reset counted as a crash** → no impulse for 0.5 s after any reset, impulses clamped. Pinned in Task 1.
4. **Driving off the generated tile / falling through a gap** → detected (AC car > 5 m below GTA ground), on-screen "press R to reset". Pinned in Task 1 (`isOffSurface`) and Task 7.
5. **Player leaves the car, dies or is arrested** → link turns off cleanly, no crash, no orphan puppet. Pinned in Task 7 step 5.

---

### Task 0 [PC]: Spike A — AC keeps wheel input and FFB in the background (go/no-go)

No code. Done first, before anything else is built.

- [ ] **Step 1:** Start AC (any car, any track, practice) through Content Manager, windowed 1280×720.
- [ ] **Step 2:** Start GTA V Enhanced story mode, borderless window, focus GTA.
- [ ] **Step 3:** With GTA focused, steer and throttle. Check in AC (Alt-Tab after 30 s): did the car move? Did FFB stay on during the 30 s?
- [ ] **Step 4:** If not: try in order (a) CSP setting "Keep input active when unfocused" / AC `[INPUT]` background options, (b) AC fullscreen-windowed on a second monitor, (c) virtual joystick feeder (vJoy) forwarding the wheel while AC keeps the FFB device. Record which works.
- [ ] **Step 5:** Write the result into `docs/spikes.md` (what worked, perceived lag). **If nothing works without lag you can feel: stop and decide with the owner.**

---

### Task 1: Link math (C++, Linux-tested)

**Files:**
- Create: `common/link_math.hpp`, `common/link_math.cpp`, `tests/test_link_math.cpp`, `tests/CMakeLists.txt`, `CMakeLists.txt`, `.gitignore`, `third_party/doctest.h` (vendored v2.4.11)

**Interfaces:**
- Produces:
  - `struct Vec3 { float x, y, z; }`, `struct Quat { float x, y, z, w; }`
  - `Vec3 acToGta(Vec3)`, `Vec3 gtaToAc(Vec3)`
  - `Quat acBasisToGtaQuat(Vec3 acForward, Vec3 acUp)` — GTA quaternion whose +Y = forward, +Z = up
  - `struct Pose { Vec3 pos; Quat rot; Vec3 vel; Vec3 angVel; }` (GTA space)
  - `Pose predict(const Pose&, float ageSeconds)` — age clamped to `[0, 0.05]`
  - `struct Impulse { Vec3 point; Vec3 impulse; }` (AC space, N·s)
  - `std::optional<Impulse> hitImpulse(Vec3 vCommandedGta, Vec3 vAfterGta, float dt, float massKg, Vec3 contactPointGta, float secondsSinceReset)`
  - `bool isOffSurface(float acCarHeightGta, float gtaGroundZ)`

- [ ] **Step 1: Write failing tests** in `tests/test_link_math.cpp`:
  - `acToGta({1,2,3}) == {1,-3,2}`; `gtaToAc(acToGta(v)) == v` for 3 random vectors.
  - `acBasisToGtaQuat(fwd=gtaToAc({0,1,0}), up=gtaToAc({0,0,1}))` ≈ identity; fwd = gtaToAc({1,0,0}) gives yaw −90° about Z (GTA heading 270).
  - `predict` with vel {10,0,0}, age 0.02 → pos.x += 0.2; age 0.5 → clamped, pos.x += 0.5.
  - `hitImpulse`: dv below 0.5 m/s (after removing gravity `-9.81*dt` on z) → `nullopt`; dv {−3,0,0}, mass 1200 → impulse magnitude 3600 converted to AC space; dv 100 m/s → clamped to `30 * mass`; `secondsSinceReset 0.2` → `nullopt`.
  - `isOffSurface(-10, 0)` true, `isOffSurface(-4, 0)` false (threshold 5 m).
- [ ] **Step 2:** `cmake -S . -B build && cmake --build build && ctest --test-dir build` → FAIL (undefined symbols).
- [ ] **Step 3:** Implement in `common/link_math.cpp`. Quaternion from basis: right = fwd × up, build rotation matrix columns (right, fwd, up), standard matrix→quat.
- [ ] **Step 4:** Run ctest → all PASS.
- [ ] **Step 5:** Commit `feat: link math (coords, prediction, hit impulse)`.

---

### Task 2: Shared-memory protocol + seqlock (C++, Linux-tested)

**Files:**
- Create: `common/link_protocol.h`, `common/seqlock.hpp`, `tests/test_seqlock.cpp`
- Create: `csp/link_layout.lua` (same layout as `ac.connect` items — must match the header field for field)

**Interfaces:**
- Produces:
  - `constexpr uint32_t LINK_MAGIC = 0x47544143; // "GTAC"`, `LINK_VERSION = 1`
  - `struct LinkState { uint32_t magic, version, seq; uint32_t resetCounter; float pos[3], fwd[3], up[3], vel[3], angVel[3]; float wheelSpin[4]; float steerDeg, rpm, speedKmh, massKg; int32_t gear; };` (AC space, written by CSP)
  - `struct HitBlock { uint32_t seq; uint32_t count; struct { float point[3], impulse[3]; } items[16]; };` (AC space, written by `.asi`; ring, `count` monotonic)
  - `template<class T> bool seqRead(const volatile T* src, T& out)` — retries up to 4 times, false if seq odd or changed
  - `class StalenessTracker { void onRead(uint32_t seq, double nowSec); bool stale(double nowSec) const; }` — stale when seq unchanged ≥ 0.25 s
  - Shared-memory names: `LINK_STATE_NAME`, `LINK_HITS_NAME` — set to the names CSP's `ac.connect` (`ac.SharedNamespace.Shared`) maps to; look these up in the CSP Lua SDK docs and record them in the header comment.

- [ ] **Step 1: Write failing tests:** writer thread toggling seq odd→even with two distinct payloads, reader asserts every successful read is one of the two payloads (10 000 iterations); `StalenessTracker` stale at 0.25 s, fresh again after a new seq; `static_assert(sizeof(LinkState) == 112)` and `offsetof` checks for each field.
- [ ] **Step 2:** Run ctest → FAIL.
- [ ] **Step 3:** Implement. Writers: seq++ (odd), write, seq++ (even), with `std::atomic_thread_fence` around.
- [ ] **Step 4:** Run ctest → PASS.
- [ ] **Step 5:** Commit `feat: link protocol and seqlock`.

---

### Task 3: Surface generator — read CodeWalker `.ybn` XML (Python)

**Files:**
- Create: `tools/surfacegen/ybn_xml.py`, `tools/surfacegen/tests/test_ybn_xml.py`, `tools/surfacegen/tests/fixtures/small.ybn.xml` (hand-written, 2 triangles + 1 box, invented coordinates), `tools/surfacegen/pyproject.toml`

**Interfaces:**
- Produces: `@dataclass Tri: a: tuple[float,float,float]; b: ...; c: ...; material: str` (GTA space, absolute) and `def read_ybn_xml(path: Path) -> list[Tri]`

- [ ] **Step 1: Failing tests:** fixture yields 2 + 12 tris (box → 12 tris); vertices are absolute (geometry center + vertex, then child composite transform applied); material names read from the `Materials` list by polygon index (e.g. `"TARMAC"`); spheres/capsules/cylinders are skipped, with their count logged.
- [ ] **Step 2:** `pytest tools/surfacegen -q` → FAIL.
- [ ] **Step 3:** Implement with `xml.etree.ElementTree`. Handle `Bound` types Composite → Children → Geometry / GeometryBVH; polygons `Triangle` and `Box`.
- [ ] **Step 4:** pytest → PASS.
- [ ] **Step 5:** Commit `feat(surfacegen): read ybn xml`.

---

### Task 4: Surface generator — materials, walls, transform, tiling

**Files:**
- Create: `tools/surfacegen/surfaces.py`, `tools/surfacegen/materials.toml`, `tools/surfacegen/tests/test_surfaces.py`

**Interfaces:**
- Consumes: `Tri`, `read_ybn_xml`
- Produces: `def classify(tri: Tri, table: dict[str,str]) -> str` returns one of `ROAD KERB GRASS SAND GRAVEL WALL`; `def build_meshes(tris, table, bbox_gta: tuple[float,float,float,float] | None) -> dict[str, Mesh]` with `Mesh(vertices: list[Vec3 AC space], indices: list[int])`

- [ ] **Step 1: Failing tests:** slope > 60° → `WALL` whatever the material; `TARMAC` → `ROAD`, `GRASS` → `GRASS`, unknown material → `ROAD` with a warning; vertices come out through `AC = (x, z, -y)`; triangle winding flipped so normals point up in AC (Y-up); a bbox drops triangles whose centroid is outside it; vertices de-duplicated per mesh.
- [ ] **Step 2:** pytest → FAIL. **Step 3:** implement; `materials.toml` maps every GTA material in CodeWalker's material list to an AC key (start with ~20 common ones, rest default `ROAD`). **Step 4:** pytest → PASS. **Step 5:** commit `feat(surfacegen): classify and build meshes`.

---

### Task 5: Surface generator — write AC track folder

**Files:**
- Create: `tools/surfacegen/kn5.py`, `tools/surfacegen/track.py`, `tools/surfacegen/cli.py`, `tools/surfacegen/tests/test_kn5.py`, `tools/surfacegen/tests/test_track.py`

**Interfaces:**
- Consumes: `build_meshes`
- Produces:
  - `def write_kn5(path, meshes: dict[str, Mesh], dummies: dict[str, Mat4])` and `def read_kn5(path) -> (meshes, dummies)` (test-only reader)
  - `python -m surfacegen --xml-dir work/xml --out <AC>/content/tracks/gta_ls --layout downtown --bbox x0,y0,x1,y1 --start x,y,z,heading`

- [ ] **Step 1: Failing tests:** round trip `write_kn5` → `read_kn5` keeps every mesh, vertex count, index list and the dummies `AC_START_0`, `AC_PIT_0`, `AC_HOTLAP_START_0`, `AC_TIME_0_L`, `AC_TIME_0_R`; mesh nodes are named `1ROAD_0`, `1WALL_0`… and flagged not renderable; meshes over 65 535 vertices are split (`1ROAD_0`, `1ROAD_1`); `track.py` writes `data/surfaces.ini` with one `[SURFACE_n]` per key used, `models_downtown.ini`, `ui/downtown/ui_track.json`.
- [ ] **Step 2:** pytest → FAIL.
- [ ] **Step 3:** Implement kn5 v5 following Content Manager's open-source `AcTools.Kn5File` (github.com/gro-ove/actools) as the format reference. One shared material with no textures. `surfaces.ini` values copied from a Kunos track's ROAD/KERB/GRASS/SAND entries; WALL = `KEY=WALL`, `IS_VALID_TRACK=0`, friction 0.
- [ ] **Step 4:** pytest → PASS. **Step 5:** commit `feat(surfacegen): write AC track`.

---

### Task 6 [PC]: Spike B — generated downtown tile drives in AC

- [ ] **Step 1:** In CodeWalker (pointed at the GTA V Enhanced folder), export the `.ybn` files for downtown Los Santos as XML into `work/xml/` (gitignored).
- [ ] **Step 2:** `python -m surfacegen --xml-dir work/xml --out "<AC>/content/tracks/gta_ls" --layout downtown --bbox -400,-1200,600,0 --start 215,-810,30,160` (Legion Square area; adjust after looking).
- [ ] **Step 3:** In Content Manager: load `gta_ls / downtown`, practice. Use CSP's free camera with "show physics meshes" (or the CSP debug app) to see the surfaces.
- [ ] **Step 4:** Check: car spawns on the road, drives, kerbs/grass feel different, buildings block. Note load time and RAM.
- [ ] **Step 5:** Pick one landmark (e.g. a building corner): read its GTA position in CodeWalker, check AC's position for the same spot through the transform. They should match within 0.5 m. Record in `docs/spikes.md`. Commit any fixes.

---

### Task 7 [PC]: CSP car script + GTA `.asi` — Spike C and Stage 1

**Files:**
- Create: `csp/gta_link/script.lua` (CSP car script, installed into a *copy* of the chosen AC car: `<AC>/content/cars/<car>_gtalink/extension/script.lua` + `ext_config.ini` `[SCRIPT_...]` entry)
- Create: `gta/src/main.cpp`, `gta/src/puppet.cpp`, `gta/src/puppet.hpp`, `gta/src/hud.cpp`, `gta/CMakeLists.txt` (MSVC, links ScriptHookV SDK `ScriptHookV.lib` from `third_party/shv/`, which the owner downloads; not committed)
- Create: `docs/install.md`

**Interfaces:**
- Consumes: `LinkState`, `seqRead`, `StalenessTracker`, `acToGta`, `acBasisToGtaQuat`, `predict`, `isOffSurface`, `link_layout.lua`
- Produces: `class Puppet { void link(); void unlink(); void tick(double nowSec); bool linked() const; }`, hotkeys F8 = link/unlink, R = reset

- [ ] **Step 1: CSP script:** every physics step (`script.update(dt)`), write `car.position`, `car.look`, `car.up`, `car.velocity`, `car.localAngularVelocity` (rotated to world), wheel spin, steer, rpm, gear, speed, mass into the shared `LinkState` (seq odd→even). Increase `resetCounter` on teleport. Check with a small Python reader (`tools/linkpeek.py`, mmap by name) that values change while driving.
- [ ] **Step 2: Puppet link:** on F8, spawn vehicle model `"sultan"` (default; configurable in `gta_link.ini`) at the converted AC position, put the player in it, `SET_ENTITY_COLLISION(true)`, mute engine audio, disable vehicle control actions every frame.
- [ ] **Step 3: Puppet tick:** every frame: `seqRead` → age = now − time the seq last changed → `predict` → `SET_ENTITY_COORDS_NO_OFFSET`, `SET_ENTITY_QUATERNION`, `SET_ENTITY_VELOCITY`, angular velocity. HUD: speed, gear, rpm from `LinkState`. Spike C: AC on any flat track, GTA puppet at the airport runway with an offset. Record GTA FPS with AC running in `docs/spikes.md`.
- [ ] **Step 4: Stage 1 on downtown tile:** no offset. Drive 10 minutes. Check: no visible jitter, car sits on GTA's road (±0.1 m visually), FFB normal. `isOffSurface` → HUD "Off the map — press R". R → CSP teleports the AC car to the nearest road node in GTA (`GET_CLOSEST_VEHICLE_NODE_WITH_HEADING`), converted to AC, puppet follows.
- [ ] **Step 5: Review Focus checks (manual):** close AC mid-drive → puppet freezes, HUD "AC not running", reopen → relinks by itself. Get out of car / die / get arrested / trainer teleport or vehicle swap (puppet jumps > 20 m in one frame) → `unlink()` runs, puppet deleted, no crash. Pause menu → no crash, snaps back on resume.
- [ ] **Step 6:** Grow the tile to the full map (or tiles per area if load time > 60 s or RAM > 4 GB). Write `docs/install.md` (ScriptHookV for Enhanced, `.asi` copy, AC car copy, track generation). Commit `feat: stage 1 — AC physics drives GTA puppet`.

---

### Task 8 [PC]: Stage 2 — GTA hits felt in AC

**Files:**
- Modify: `gta/src/puppet.cpp` (hit detection after each tick), `csp/gta_link/script.lua` (apply hits)
- Test: `tests/test_link_math.cpp` already covers `hitImpulse`; add `tests/test_hitblock.cpp` for the ring (16 items, `count` monotonic, reader catches up without skipping or repeating)

**Interfaces:**
- Consumes: `hitImpulse`, `HitBlock`, `seqRead`
- Produces: `gta_link.ini` keys `hit_gain = 1.0`, `hit_min_dv = 0.5`, `hit_max_dv = 30`

- [ ] **Step 1:** Failing ring test → implement writer/reader helpers in `common/hitring.hpp` → PASS → commit.
- [ ] **Step 2:** In `Puppet::tick`, before overwriting: read GTA velocity; if `HAS_ENTITY_COLLIDED_WITH_ANYTHING` and `hitImpulse(...)` returns a value, push it into `HitBlock`. Contact point: last collision normal × half car width from centre.
- [ ] **Step 3:** CSP script: each step, read new `HitBlock` items and apply each with `physics.addForce` at the point, as impulse ÷ dt for one step.
- [ ] **Step 4 (manual):** hit a lamp post at 50 km/h: post breaks, car slows a bit, wheel jolts. Side-swipe a traffic car: both deflect. Hit a wall that's only in GTA (e.g. a parked truck): car stops. Pedestrian: ragdolls, small bump. Reset: no jolt. Tune `hit_gain`; record values in `docs/spikes.md`.
- [ ] **Step 5:** Commit `feat: stage 2 — collisions feed back into AC`.
