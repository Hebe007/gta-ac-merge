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
