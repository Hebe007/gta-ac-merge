# GTA V × Assetto Corsa merge — Design

Date: 2026-11-17 · Own repo: gta-ac-merge

## 1. Intent

Drive Assetto Corsa's car physics — with its force feedback and wheel/pedal handling — through the
**original, current GTA V world**: the real Los Santos and Blaine County, GTA's traffic, pedestrians,
lighting and weather. The existing GTA map ports for AC look poor and are unmaintained, and AC's own
traffic options are thin. GTA already has the city and the life in it; AC has the driving. This merges them.

Single-player, offline, on the owner's own PC with their own copies of both games.

## 2. Goals, non-goals, anti-goals

**Goals, in order (stages):**
1. **Stage 1 — Drive AC physics in normal GTA.** You drive with your wheel; AC calculates the car; GTA shows
   it in its own world. The world behaves like normal GTA, untouched.
2. **Stage 2 — The car is a normal GTA car to the world.** Pedestrians can be hit, traffic lights/poles and
   walls get hit and break, traffic reacts to you and you can bump into it — and those hits are felt in AC's
   physics and force feedback.
3. **Stage 3 — Police chases** and the rest of GTA's wanted system working with the AC car. Last effort.

**Non-goals:**
- GTA Online, FiveM, AC multiplayer servers, any online play.
- Shipping or sharing any Rockstar asset. Everything derived from GTA files is generated on the owner's PC
  and stays there.
- Rebuilding GTA's map for AC as something you look at.
- **Story mode.** Free roam only: load a save, link the AC car, drive. Missions, cutscenes, getting out and
  switching cars are not handled (the link is simply switched off for those). May come later.
- Making GTA's own cars drive with AC physics (only the player's car).

**Anti-goals (failure even if it "works"):**
- The car feels worse than in AC: mushy or delayed FFB, input lag, physics glitches from a bad road surface.
- Visible jitter or rubber-banding of the player car in GTA.
- Anything that risks the owner's Rockstar account (online use with mods).

## 3. Constraints

- Windows PC, owner's copies of GTA V and Assetto Corsa. AC already has Content Manager + Custom Shaders
  Patch (CSP), version 2.11 as reported by the owner. GTA V Enhanced from the Epic Games Store, game version
  1.0.1158.16_v7.
- Both games run at the same time on one PC → performance budget is shared. AC must be kept as cheap as
  possible (it is never looked at).
- GTA modding via ScriptHookV (single-player only). **The owner has GTA V Enhanced with ScriptHookV already
  working** (a trainer runs on it). Build against the ScriptHookV SDK for Enhanced. The trainer can stay
  installed; its teleports/vehicle spawns while linked desync the puppet, so the link turns off when the
  player's vehicle changes or jumps more than 20 m in one frame.
- AC shared memory (`acpmf_physics`, `acpmf_graphics`, `acpmf_static`) is the standard way to read the car's
  state from outside AC.

## 4. Approaches considered

How the AC car exists in GTA's world:

| | Approach | Trade-off |
|---|---|---|
| **A (picked)** | **Puppet GTA car.** A real GTA vehicle is spawned; every frame its position, rotation and velocity are overwritten from AC. Collisions GTA reports are sent back to AC as forces. | GTA treats it as one of its own cars, so peds, traffic reactions, damage and later police mostly come for free. Car-vs-car hits are GTA's collision response translated into an AC force — slightly less exact than AC's own; needs tuning. |
| B | Visual-only ghost drawn in GTA. | Quickest, but traffic and peds pass through you. Kills stage 2. Dropped. |
| C | Mirror nearby GTA traffic into AC as solid objects; AC resolves every hit. | Most exact car-vs-car feel, but heavy two-way sync, AC car-count limits, no help with peds or police. Kept as a possible later add-on for the few cars touching you only. |

The owner picked **A** with GTA as the host (GTA renders everything; AC runs hidden underneath).
GTA-as-host was chosen over AC-as-host because the owner wants the original GTA map, not the AC port.

## 5. The design

### 5.1 Overview

```
 wheel/pedals ──► Assetto Corsa (hidden, background)  ── FFB ──► wheel
                   │  drives on an invisible surface
                   │  generated from GTA's own collision
                   │
          shared memory: car state (pos, rot, vel, wheels, rpm, gear)  ▲ forces/impulses
                   ▼                                                    │ (stage 2)
                 GTA V + our script (the window you look at)  ──────────┘
                   puppet GTA car follows AC exactly
```

Three pieces:
1. **Surface generator** (offline tool): GTA collision → invisible AC track.
2. **GTA script** (`.asi` via ScriptHookV): spawns and moves the puppet car, reports hits.
3. **AC side**: the generated track, a fixed car setup, and a CSP Lua script that applies forces sent from
   GTA (stage 2).

### 5.2 The invisible surface (AC track generated from GTA)

- Export GTA's static collision (the `.ybn` bounds files) with **CodeWalker** (or a script using its
  libraries) from the owner's install.
- Convert to an AC track: physics-only meshes named with AC's surface prefixes (`1ROAD`, `1KERB`, `1GRASS`,
  `1SAND`, `1WALL`…) and a `surfaces.ini`. GTA collision material IDs are mapped to AC surfaces
  (asphalt/concrete → road, grass/dirt → grass/dirt, etc.). No visual meshes beyond what AC requires.
- **Coordinates:** GTA is Z-up, AC is Y-up, both in metres. Fixed transform: `AC = (gta.x, gta.z, -gta.y)`
  (exact sign/axis to be verified on day one with a known point). Because the surface comes from GTA's own
  data, no further calibration is needed — positions map 1:1.
- **Size:** the full map is ~8 × 12 km. Start with a **downtown Los Santos tile**, then grow. If the full map
  hurts AC's loading, memory or float precision, split into tiles (several AC "layouts") and pick by area.
- Only **static** collision goes into AC. Lamp posts, traffic lights, fences, bins and parked cars are
  dynamic objects in GTA — AC doesn't know about them; GTA handles those hits (stage 2).
- Optional pit/start position near a chosen GTA spot so sessions start somewhere sensible.

### 5.3 AC side

- Runs in a small window, lowest graphics, CSP effects off — it is never looked at.
- Session: practice/hotlap on the generated track, no AI, no damage at first.
- **Input and FFB stay in AC.** The wheel is bound in AC as normal. GTA's own vehicle controls are disabled
  so the wheel doesn't also drive GTA.
- **Car audio from AC** (engine, tyres) — AC keeps playing in the background; GTA's engine sound for the
  puppet is muted. World audio from GTA. (Owner can flip this later.)
- **Stage 2:** a CSP Lua script receives impulses from GTA and applies them to the car (CSP physics API,
  e.g. add force at a point). It can also teleport the car (respawn, "reset to road").

### 5.4 GTA side (the script)

- C++ `.asi` loaded by ScriptHookV (C++ for low overhead and direct shared-memory reads; ScriptHookVDotNet
  is the fallback if faster iteration is needed).
- On start: spawn a GTA car of similar size/class to the AC car, put the player in it, disable vehicle
  controls, make its own engine silent.
- **Every GTA frame:** read the latest AC state from shared memory, convert coordinates, and set the puppet's
  position, rotation, velocity and angular velocity. Use a small **prediction** from velocity to cover the
  time between AC's physics step and GTA's frame, so the car doesn't stutter.
- Wheels: steer angle and spin from AC's state where GTA allows it.
- Camera: GTA's normal chase/first-person cameras. A minimal speed/gear/RPM readout from AC's data drawn
  in GTA (AC's HUD apps are not visible).
- Hotkeys: toggle the link on/off, reset car to road (teleports AC car and puppet together).

### 5.5 Stage 2 — collisions back into AC

- Each frame the puppet is first set to AC's state; GTA then runs its physics. If GTA's resulting velocity
  differs from what AC commanded (something pushed it — a car, a pole, a pedestrian, a wall that only
  exists in GTA), that **difference is the hit**. Convert it to an impulse at the contact point and send it
  to AC; AC applies it and the wheel feels it.
- Large GTA-only obstacles (parked cars, barriers) behave like walls through the same path.
- Damage: GTA's visual damage shows as normal. AC mechanical damage is optional, off at first.
- Peds, traffic reactions and breakable props are GTA's own behaviour once the puppet is a real car.

### 5.6 Stage 3 — police

- Wanted levels and chases should work because the puppet is a normal GTA vehicle. Work here is mostly
  making sure AC-driven movement doesn't confuse GTA's systems (e.g. "player in vehicle" checks, PIT
  manoeuvres pushing the car = stage 2 impulses).

### 5.7 Transport between the games

- AC → GTA: a shared-memory block written by our CSP car script at physics rate (full position, rotation,
  velocity), read directly by the GTA `.asi` (no extra process). AC's standard `acpmf_*` shared memory is the
  fallback (heading/pitch/roll + coordinates; may update slowly while AC is in the background).
- GTA → AC (stage 2): a small named shared-memory block (or local UDP) the CSP Lua script reads.
  Which one is decided by a spike, based on what CSP Lua can read reliably.

## 6. Milestones

0. **Spikes (each a yes/no before building):**
   a. AC keeps taking wheel input, running physics and FFB while **not** the focused window (GTA on top).
   b. CodeWalker export of one area → AC track that loads and is drivable.
   c. Puppet test: AC on a flat test track, GTA puppet on the airport runway following it, offset only.
1. **Stage 1:** downtown tile → full map; smooth puppet; hotkeys; AC audio.
2. **Stage 2:** hit detection and impulses into AC; tune feel.
3. **Polish (optional):** AC car models converted to GTA format (Blender + Sollumz), cockpit camera aligned
   to AC's driver position, nicer HUD.
4. **Stage 3:** police.

## 7. Risks

- **Background input (spike a).** If AC stops reading the wheel or pauses when unfocused, a workaround is
  needed (CSP/AC settings, input forwarding, or window tricks). Highest risk; tested first.
- **Performance:** two games at once. Mitigated by AC at minimum settings and small window.
- **Surface quality:** GTA collision is lower detail than its visuals; small bumps may differ from what's
  seen. Acceptable; tune if it bothers.
- **Edition/updates:** GTA updates can break ScriptHookV until it's updated. Keep GTA offline/unpatched
  while playing.
- **Hit feel (approach A):** car-vs-car hits are GTA's response translated; may need tuning or option C
  for close cars.

## 8. Calls made by the builder (owner can override)

- C++ `.asi` for GTA; CSP Lua on AC side; Python for the map tooling.
- Start car: one AC car + one matching GTA car pairing.
- AC audio for the player's car, GTA audio for the world.
- Start with a downtown tile, not the full map.
- AC damage off at first.
- **Spike (a) fallback:** first AC/CSP settings and window tricks, then input forwarding (e.g. a virtual
  joystick feeder) while AC keeps FFB. If every option adds lag you can feel, the project pauses for a
  decision with the owner, not a quiet switch to AC-as-host.
- **Stage 2 receiver:** a CSP car script on a *copy* of the chosen AC car (car scripts are allowed to add
  forces and teleport). Installed CSP version checked at the first session at the PC.
- **Performance:** no fixed FPS target up front; spike (c) measures GTA FPS with AC running, and that sets the
  tile size and how much prediction the puppet uses.
