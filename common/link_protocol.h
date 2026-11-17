// Shared-memory blocks between the CSP car script (AC) and the GTA .asi.
// Layout must match csp/link_layout.lua field for field (tools/tests/test_layout_match.py checks it).
// All vectors are AC world space (Y-up, metres). Writers use the seqlock in seqlock.hpp.
#pragma once
#include <cstdint>

constexpr uint32_t LINK_MAGIC = 0x47544143;  // "GTAC"
constexpr uint32_t LINK_VERSION = 5;

// Names of the memory-mapped files, opened by CSP's ac.writeMemoryMappedFile. CSP's own examples use the
// "AcTools.CSP." prefix, so we do too. tools/linkpeek.py shows whether the GTA side sees the data under this name;
// the GTA side can override it with shm_name in gta_link.ini.
constexpr const char* LINK_STATE_NAME = "AcTools.CSP.GtaAcLink.State.v1";
constexpr const char* LINK_HITS_NAME = "AcTools.CSP.GtaAcLink.Hits.v1";
constexpr const char* LINK_CMD_NAME = "AcTools.CSP.GtaAcLink.Cmd.v1";

// Written by AC (CSP car script) every physics step.
struct LinkState {
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t resetCounter;
  float pos[3];
  float fwd[3];
  float up[3];
  float vel[3];
  float angVel[3];
  float wheelSpin[4];
  float steerDeg;
  float rpm;
  float speedKmh;
  float massKg;
  int32_t gear;  // CSP numbering: -1 reverse, 0 neutral, 1 first
  float rideHeight;  // metres from pos down to the tyre contact plane, along up (0 = unknown)
  float steerLockDeg;  // steering wheel angle at full lock (0 = unknown); steerDeg / steerLockDeg = -1..1
  float gas;    // throttle pedal 0..1
  float brake;  // brake pedal 0..1
  uint32_t teleportAck;    // last LinkCommand.teleportId the AC app handled
  int32_t teleportStatus;  // for that id: 1 = moved, -1 = CSP refused
};

// Written by the GTA .asi: requests for the AC app (AC space).
struct LinkCommand {
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t teleportId;  // new id = please move the AC car
  float pos[3];
  float fwd[3];
};

struct HitItem {
  float point[3];
  float impulse[3];
};

// Written by the GTA .asi. Ring of 16; count only ever grows, item i lives at items[i % 16].
struct HitBlock {
  uint32_t seq;
  uint32_t count;
  HitItem items[16];
};
