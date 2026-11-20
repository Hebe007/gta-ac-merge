#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "link_math.hpp"
#include <cmath>

static void checkVec(Vec3 a, Vec3 b, float eps = 1e-4f) {
  CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
  CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
  CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
}
static float len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

static float length2(Vec3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

TEST_CASE("acToGta maps axes") { checkVec(acToGta({1, 2, 3}), {1, -3, 2}); }

TEST_CASE("gtaToAc inverts acToGta") {
  for (Vec3 v : {Vec3{1.5f, -2, 7}, Vec3{-300, 12.25f, 0.5f}, Vec3{0, 0, -9}})
    checkVec(gtaToAc(acToGta(v)), v);
}

TEST_CASE("basis to quat: GTA north/up is identity") {
  Quat q = acBasisToGtaQuat(gtaToAc({0, 1, 0}), gtaToAc({0, 0, 1}));
  CHECK(std::fabs(q.w) == doctest::Approx(1).epsilon(1e-4));
  CHECK(q.x == doctest::Approx(0).epsilon(1e-4));
  CHECK(q.y == doctest::Approx(0).epsilon(1e-4));
  CHECK(q.z == doctest::Approx(0).epsilon(1e-4));
}

TEST_CASE("basis to quat: facing GTA +X is yaw -90 about Z") {
  Quat q = acBasisToGtaQuat(gtaToAc({1, 0, 0}), gtaToAc({0, 0, 1}));
  float s = q.w < 0 ? -1.f : 1.f;  // q and -q are the same rotation
  CHECK(s * q.x == doctest::Approx(0).epsilon(1e-4));
  CHECK(s * q.y == doctest::Approx(0).epsilon(1e-4));
  CHECK(s * q.z == doctest::Approx(-0.70710678f).epsilon(1e-4));
  CHECK(s * q.w == doctest::Approx(0.70710678f).epsilon(1e-4));
}

TEST_CASE("predict moves by velocity and clamps age") {
  Pose p{{0, 0, 0}, {0, 0, 0, 1}, {10, 0, 0}, {0, 0, 0}};
  CHECK(predict(p, 0.02f).pos.x == doctest::Approx(0.2f));
  CHECK(predict(p, 0.5f).pos.x == doctest::Approx(0.5f));
  CHECK(predict(p, -1.f).pos.x == doctest::Approx(0.f));
}

TEST_CASE("predict rotates by angular velocity") {
  Pose p{{0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 10}};  // 10 rad/s yaw
  Quat q = predict(p, 0.05f).rot;                          // 0.5 rad
  CHECK(q.z == doctest::Approx(std::sin(0.25f)).epsilon(1e-3));
  CHECK(q.w == doctest::Approx(std::cos(0.25f)).epsilon(1e-3));
}

TEST_CASE("hitImpulse ignores gravity-only change") {
  float dt = 1.f / 60;
  CHECK_FALSE(hitImpulse({0, 0, 0}, {0, 0, -kGravity * dt}, dt, 1200, {0, 0, 0}, 10).has_value());
  CHECK_FALSE(hitImpulse({0, 0, 0}, {0.3f, 0, -kGravity * dt}, dt, 1200, {0, 0, 0}, 10).has_value());
}

TEST_CASE("hitImpulse converts a real hit to AC space") {
  float dt = 1.f / 60;
  auto h = hitImpulse({10, 0, 0}, {7, 0, -kGravity * dt}, dt, 1200, {1, 2, 3}, 10);
  REQUIRE(h.has_value());
  checkVec(h->impulse, gtaToAc({-3600, 0, 0}));
  checkVec(h->point, gtaToAc({1, 2, 3}));
}

TEST_CASE("hitImpulse clamps huge hits") {
  float dt = 1.f / 60;
  auto h = hitImpulse({0, 0, 0}, {0, 100, -kGravity * dt}, dt, 1200, {0, 0, 0}, 10);
  REQUIRE(h.has_value());
  CHECK(len(h->impulse) == doctest::Approx(30.f * 1200));
}

TEST_CASE("hitImpulse is quiet right after a reset") {
  float dt = 1.f / 60;
  CHECK_FALSE(hitImpulse({10, 0, 0}, {0, 0, 0}, dt, 1200, {0, 0, 0}, 0.2f).has_value());
}

TEST_CASE("isOffSurface threshold 5 m") {
  CHECK(isOffSurface(-10, 0));
  CHECK_FALSE(isOffSurface(-4, 0));
}

TEST_CASE("hitImpulse: resting on the ground during a long frame is not a hit") {
  float dt = 0.1f;  // 100 ms hitch; ground support cancels GTA's gravity step
  CHECK_FALSE(hitImpulse({5, 0, 0}, {5, 0, 0}, dt, 1500, {0, 0, 0}, 10).has_value());
}

TEST_CASE("hitImpulse: a real upward knock still counts") {
  float dt = 1.f / 60;
  auto h = hitImpulse({0, 0, 0}, {0, 0, 2}, dt, 1000, {0, 0, 0}, 10);
  REQUIRE(h.has_value());
  checkVec(h->impulse, gtaToAc({0, 0, 2000}));
}

TEST_CASE("angularVelocity recovers a yaw rate from two orientations") {
  Quat a{0, 0, 0, 1};
  Quat b{0, 0, std::sin(0.05f), std::cos(0.05f)};  // 0.1 rad about Z
  Vec3 w = angularVelocity(a, b, 0.01f);           // over 10 ms -> 10 rad/s
  checkVec(w, {0, 0, 10}, 1e-3f);
  Quat negB{-b.x, -b.y, -b.z, -b.w};               // same rotation, other hemisphere
  checkVec(angularVelocity(a, negB, 0.01f), {0, 0, 10}, 1e-3f);
  checkVec(angularVelocity(a, a, 0.01f), {0, 0, 0});
}

TEST_CASE("gtaHeadingDeg: 0 north, 90 west, 270 east") {
  CHECK(gtaHeadingDeg({0, 1, 0}) == doctest::Approx(0).epsilon(1e-4));
  CHECK(gtaHeadingDeg({-1, 0, 0}) == doctest::Approx(90).epsilon(1e-4));
  CHECK(gtaHeadingDeg({1, 0, 0}) == doctest::Approx(270).epsilon(1e-4));
}

TEST_CASE("validBasis rejects zeroed or parallel vectors") {
  CHECK(validBasis({0, 0, 1}, {0, 1, 0}));
  CHECK_FALSE(validBasis({0, 0, 0}, {0, 1, 0}));
  CHECK_FALSE(validBasis({0, 1, 0}, {0, 1, 0}));
}

TEST_CASE("rotate turns a vector by a quaternion") {
  Quat yaw90{0, 0, std::sin(0.785398f), std::cos(0.785398f)};  // +90 deg about Z
  checkVec(rotate(yaw90, {0, 1, 0}), {-1, 0, 0}, 1e-4f);
  checkVec(rotate({0, 0, 0, 1}, {0, 0, 1}), {0, 0, 1});
}

TEST_CASE("followVelocity: drives toward the target, snaps when far behind") {
  Follow f = followVelocity({10, 0, 0}, {9.5f, 0, 0}, {20, 0, 0}, 10.f, 2.f);
  CHECK_FALSE(f.snap);
  checkVec(f.vel, {25, 0, 0});  // 20 m/s plus 0.5 m error * 10 /s
  CHECK(followVelocity({10, 0, 0}, {5, 0, 0}, {0, 0, 0}, 10.f, 2.f).snap);
}

TEST_CASE("steerInputValue: 0.5 centre, 0 full left, 1 full right") {
  CHECK(steerInputValue(0.f) == doctest::Approx(0.5f));
  CHECK(steerInputValue(1.f) == doctest::Approx(0.f));   // bias +1 = full left
  CHECK(steerInputValue(-1.f) == doctest::Approx(1.f));
}

TEST_CASE("collisionDv: into a parked car of the same weight at 20 m/s loses about 60% of the speed") {
  // we go +x at 20 m/s; GTA pushed us back (-x); the other car (same mass) now moves +x at 5 m/s
  Vec3 dv = collisionDv({20, 0, 0}, {15, 0, 0}, {5, 0, 0}, 1300, 1300, {-1, 0, 0});
  // other before = 5 + (-5) = 0 (parked); closing 20; dv = 1.25 * 20 * 0.5 = 12.5 back
  CHECK(dv.x == doctest::Approx(-12.5f));
  CHECK(dv.y == doctest::Approx(0));
}

TEST_CASE("collisionDv: a heavy truck stops you more, a pedestrian barely") {
  Vec3 truck = collisionDv({20, 0, 0}, {15, 0, 0}, {0.7f, 0, 0}, 1300, 9000, {-1, 0, 0});
  Vec3 ped = collisionDv({20, 0, 0}, {19.9f, 0, 0}, {12, 0, 0}, 1300, 80, {-1, 0, 0});
  CHECK(-truck.x > 18.f);
  CHECK(-ped.x < 3.f);
}

TEST_CASE("collisionDv: already moving apart, or GTA pressing us back again, gives nothing") {
  // a second frame of the same contact: we're already slower than the other car
  CHECK(length2(collisionDv({8, 0, 0}, {6, 0, 0}, {12, 0, 0}, 1300, 1300, {-1, 0, 0})) == doctest::Approx(0));
}

TEST_CASE("collisionDv: hit from the side by a car is a push sideways, never up") {
  // standing still; a car at 15 m/s from -y hits us; GTA pushed us +y and a bit up
  Vec3 dv = collisionDv({0, 0, 0}, {0, 4, 1}, {0, 11, 0}, 1300, 1500, {0, 1, 0});
  CHECK(dv.y > 5.f);
  CHECK(dv.z == doctest::Approx(0));
}
