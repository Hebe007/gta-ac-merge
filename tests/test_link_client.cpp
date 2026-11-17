#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "link_client.hpp"
#include <cmath>

static LinkState acState(Vec3 posAc, Vec3 fwdAc, Vec3 velAc = {0, 0, 0}) {
  LinkState s{};
  s.magic = LINK_MAGIC;
  s.version = LINK_VERSION;
  s.pos[0] = posAc.x; s.pos[1] = posAc.y; s.pos[2] = posAc.z;
  s.fwd[0] = fwdAc.x; s.fwd[1] = fwdAc.y; s.fwd[2] = fwdAc.z;
  s.up[1] = 1;
  s.vel[0] = velAc.x; s.vel[1] = velAc.y; s.vel[2] = velAc.z;
  s.speedKmh = 50; s.rpm = 3000; s.gear = 3; s.massKg = 1300;
  return s;
}

TEST_CASE("no shared memory means not connected") {
  LinkClient c;
  CHECK_FALSE(c.update(nullptr, 1.0).connected);
}

TEST_CASE("wrong magic or version means not connected") {
  LinkClient c;
  LinkState s = acState({0, 0, 0}, {0, 0, -1});
  s.magic = 0;
  seqWrite(&s, s);
  CHECK_FALSE(c.update(&s, 1.0).connected);
  LinkState v = acState({0, 0, 0}, {0, 0, -1});
  v.version = 99;
  seqWrite(&v, v);
  CHECK_FALSE(c.update(&v, 1.0).connected);
}

TEST_CASE("zeroed basis (AC not writing yet) means not connected") {
  LinkClient c;
  LinkState s{};
  s.magic = LINK_MAGIC; s.version = LINK_VERSION;
  seqWrite(&s, s);
  CHECK_FALSE(c.update(&s, 1.0).connected);
}

TEST_CASE("converts AC pose to GTA space with heading") {
  LinkClient c;
  LinkState s = acState({10, 30, -20}, {0, 0, -1});  // AC -Z = GTA north
  seqWrite(&s, s);
  LinkFrame f = c.update(&s, 1.0);
  REQUIRE(f.connected);
  CHECK(f.fresh);
  CHECK(f.pose.pos.x == doctest::Approx(10));
  CHECK(f.pose.pos.y == doctest::Approx(20));
  CHECK(f.pose.pos.z == doctest::Approx(30));
  CHECK(f.headingDeg == doctest::Approx(0).epsilon(1e-3));
  CHECK(f.gear == 3);
}

TEST_CASE("goes stale when AC stops updating, fresh again when it resumes") {
  LinkClient c;
  LinkState s = acState({0, 0, 0}, {0, 0, -1});
  seqWrite(&s, s);
  CHECK(c.update(&s, 1.0).fresh);
  CHECK_FALSE(c.update(&s, 1.3).fresh);  // same seq for 0.3 s
  seqWrite(&s, s);
  CHECK(c.update(&s, 1.4).fresh);
}

TEST_CASE("predicts forward between AC updates") {
  LinkClient c;
  LinkState s = acState({0, 0, 0}, {0, 0, -1}, {0, 0, -10});  // 10 m/s north
  seqWrite(&s, s);
  c.update(&s, 1.0);
  LinkFrame f = c.update(&s, 1.02);
  CHECK(f.pose.pos.y == doctest::Approx(0.2f));
}

TEST_CASE("estimates yaw rate from successive orientations") {
  LinkClient c;
  LinkState s = acState({0, 0, 0}, {0, 0, -1});
  seqWrite(&s, s);
  c.update(&s, 1.0);
  float a = 0.1f;  // turn 0.1 rad to the left (towards GTA west) in 10 ms
  LinkState t = acState({0, 0, 0}, {-std::sin(a), 0, -std::cos(a)});
  t.seq = s.seq;
  seqWrite(&s, t);
  LinkFrame f = c.update(&s, 1.01);
  CHECK(f.pose.angVel.z == doctest::Approx(10).epsilon(0.02));
}
