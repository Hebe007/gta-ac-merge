// Shared-memory blocks between the CSP car script (AC) and the GTA .asi.
// Layout must match csp/link_layout.lua field for field (tools/tests/test_layout_match.py checks it).
// All vectors are AC world space (Y-up, metres). Writers use the seqlock in seqlock.hpp.
#pragma once
#include <cstdint>

constexpr uint32_t LINK_MAGIC = 0x47544143;  // "GTAC"
constexpr uint32_t LINK_VERSION = 1;

// Names of the memory-mapped files. CSP's ac.writeMemoryMappedFile / ac.readMemoryMappedFile open these by
// name; verify the exact name the GTA side must use (any prefix CSP adds) with tools/linkpeek.py in Task 7.
constexpr const char* LINK_STATE_NAME = "GtaAcLink_State_v1";
constexpr const char* LINK_HITS_NAME = "GtaAcLink_Hits_v1";

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
  int32_t gear;
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
