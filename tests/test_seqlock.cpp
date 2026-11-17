#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "link_protocol.h"
#include "seqlock.hpp"
#include <atomic>
#include <cstddef>
#include <thread>

static_assert(sizeof(LinkState) == 116, "LinkState layout is shared with CSP Lua");
static_assert(offsetof(LinkState, magic) == 0);
static_assert(offsetof(LinkState, version) == 4);
static_assert(offsetof(LinkState, seq) == 8);
static_assert(offsetof(LinkState, resetCounter) == 12);
static_assert(offsetof(LinkState, pos) == 16);
static_assert(offsetof(LinkState, fwd) == 28);
static_assert(offsetof(LinkState, up) == 40);
static_assert(offsetof(LinkState, vel) == 52);
static_assert(offsetof(LinkState, angVel) == 64);
static_assert(offsetof(LinkState, wheelSpin) == 76);
static_assert(offsetof(LinkState, steerDeg) == 92);
static_assert(offsetof(LinkState, rpm) == 96);
static_assert(offsetof(LinkState, speedKmh) == 100);
static_assert(offsetof(LinkState, massKg) == 104);
static_assert(offsetof(LinkState, gear) == 108);
static_assert(offsetof(LinkState, rideHeight) == 112);
static_assert(sizeof(HitBlock) == 392, "HitBlock layout is shared with CSP Lua");

static LinkState filled(float v) {
  LinkState s{};
  s.magic = LINK_MAGIC; s.version = LINK_VERSION;
  for (int i = 0; i < 3; i++) s.pos[i] = s.fwd[i] = s.up[i] = s.vel[i] = s.angVel[i] = v;
  for (float& w : s.wheelSpin) w = v;
  s.steerDeg = s.rpm = s.speedKmh = s.massKg = v;
  s.gear = static_cast<int32_t>(v);
  return s;
}
static bool consistent(const LinkState& s) {
  float v = s.pos[0];
  for (int i = 0; i < 3; i++)
    if (s.pos[i] != v || s.fwd[i] != v || s.up[i] != v || s.vel[i] != v || s.angVel[i] != v) return false;
  for (float w : s.wheelSpin) if (w != v) return false;
  return s.steerDeg == v && s.rpm == v && s.speedKmh == v && s.massKg == v && s.gear == static_cast<int32_t>(v);
}

TEST_CASE("seqRead never returns a torn state") {
  static LinkState shared{};
  seqWrite(&shared, filled(1));
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int i = 0; !stop; i++) seqWrite(&shared, filled(i % 2 ? 1.f : 2.f));
  });
  int ok = 0, torn = 0;
  for (int i = 0; i < 10000; i++) {
    LinkState out;
    if (seqRead(&shared, out)) { ok++; if (!consistent(out)) torn++; }
  }
  stop = true;
  writer.join();
  CHECK(torn == 0);
  CHECK(ok > 0);
}

TEST_CASE("seqRead refuses an odd (mid-write) sequence") {
  static LinkState shared{};
  shared.seq = 3;
  LinkState out;
  CHECK_FALSE(seqRead(&shared, out));
}

TEST_CASE("StalenessTracker: stale before any read, after 0.25 s without change, fresh on new seq") {
  StalenessTracker t;
  CHECK(t.stale(0.0));
  t.onRead(2, 1.0);
  CHECK_FALSE(t.stale(1.24));
  t.onRead(2, 1.2);  // same seq does not refresh
  CHECK(t.stale(1.25));
  t.onRead(4, 1.3);
  CHECK_FALSE(t.stale(1.3));
}

TEST_CASE("a writer that died mid-write (odd seq) does not break the next writer") {
  static LinkState shared{};
  shared.seq = 3;  // previous writer crashed between its two increments
  seqWrite(&shared, filled(1));
  CHECK(shared.seq % 2 == 0);
  LinkState out;
  REQUIRE(seqRead(&shared, out));
  CHECK(consistent(out));
}
