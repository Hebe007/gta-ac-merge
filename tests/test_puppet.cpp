#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "puppet.hpp"
#include <cmath>
#include <vector>

struct FakeGame : Game {
  bool dead = false;
  int inVehicle = 0;
  int nextHandle = 7;
  std::vector<int> alive;
  Vec3 pos{0, 0, 0};
  bool externalMove = false;
  Vec3 externalPos{0, 0, 0};
  int setPoseCount = 0;
  Pose lastPose{};
  float ground = 0;
  bool hasGround = true;
  bool key = false;
  std::vector<std::string> texts;
  std::string spawnedModel;

  bool playerDeadOrArrested() override { return dead; }
  int playerVehicle() override { return inVehicle; }
  int spawnVehicle(const std::string& m, Vec3 p, float) override {
    spawnedModel = m; pos = p; alive.push_back(nextHandle); return nextHandle;
  }
  void putPlayerIn(int v) override { inVehicle = v; }
  bool exists(int v) override { for (int a : alive) if (a == v) return true; return false; }
  void deleteVehicle(int v) override { std::erase(alive, v); if (inVehicle == v) inVehicle = 0; }
  Vec3 position(int) override { return externalMove ? externalPos : pos; }
  void setPose(int, const Pose& p) override { setPoseCount++; lastPose = p; pos = p.pos; externalMove = false; }
  void disableVehicleControls() override {}
  bool groundZ(Vec3, float& z) override { z = ground; return hasGround; }
  void drawText(float, float, const std::string& t) override { texts.push_back(t); }
  bool linkKeyPressed() override { bool k = key; key = false; return k; }
  bool shown(const std::string& s) const { for (auto& t : texts) if (t.find(s) != std::string::npos) return true; return false; }
};

static LinkState shm;
static void acWrite(Vec3 posAc) {
  LinkState s{};
  s.magic = LINK_MAGIC; s.version = LINK_VERSION;
  s.pos[0] = posAc.x; s.pos[1] = posAc.y; s.pos[2] = posAc.z;
  s.fwd[2] = -1; s.up[1] = 1;
  s.speedKmh = 88; s.gear = 4; s.rpm = 4200;
  s.seq = shm.seq;
  seqWrite(&shm, s);
}

TEST_CASE("F8 links: spawns the configured car at AC's position and puts the player in") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({100, 30, -200});
  g.key = true;
  p.tick(1.0, &shm);
  REQUIRE(p.linked());
  CHECK(g.spawnedModel == "sultan");
  CHECK(g.inVehicle == 7);
  CHECK(g.pos.x == doctest::Approx(100));
  CHECK(g.pos.y == doctest::Approx(200));
}

TEST_CASE("refuses to link when it can't see AC's data at all") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  g.key = true;
  p.tick(1.0, nullptr);
  CHECK_FALSE(p.linked());
  CHECK(g.shown("can't see"));
}

TEST_CASE("refuses to link when AC's data stopped updating, and says so") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0});
  p.tick(1.0, &shm);
  g.key = true;
  p.tick(2.0, &shm);  // seen, but no update for a second
  CHECK_FALSE(p.linked());
  CHECK(g.shown("not updating"));
}

TEST_CASE("follows AC every frame and shows speed and gear") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({5, 30, 0});
  p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.x == doctest::Approx(5));
  CHECK(g.shown("88 km/h"));
  CHECK(g.shown("gear 4"));
}

TEST_CASE("AC stops: car freezes where it was and HUD says so; resumes by itself") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  p.tick(1.5, &shm);  // no new seq for 0.5 s
  CHECK(p.linked());
  CHECK(g.lastPose.vel.x == doctest::Approx(0));
  CHECK(g.lastPose.pos.x == doctest::Approx(0));
  CHECK(g.shown("waiting for Assetto Corsa"));
  acWrite({3, 30, 0});
  g.texts.clear();
  p.tick(1.6, &shm);
  CHECK(g.lastPose.pos.x == doctest::Approx(3));
  CHECK_FALSE(g.shown("waiting"));
}

TEST_CASE("leaving the car, dying or getting arrested unlinks and removes the car") {
  for (int which = 0; which < 2; which++) {
    shm = {}; FakeGame g; Puppet p(g, "sultan");
    acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
    if (which == 0) g.inVehicle = 0; else g.dead = true;
    acWrite({1, 30, 0});
    p.tick(1.016, &shm);
    CHECK_FALSE(p.linked());
    CHECK(g.alive.empty());
  }
}

TEST_CASE("a trainer teleport (car jumps > 20 m by itself) unlinks") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({1, 30, 0}); p.tick(1.016, &shm);
  g.externalMove = true; g.externalPos = {500, 500, 30};
  acWrite({2, 30, 0}); p.tick(1.032, &shm);
  CHECK_FALSE(p.linked());
  CHECK(p.message().find("teleport") != std::string::npos);
}

TEST_CASE("a reset in AC (big jump in AC's own data) does not unlink") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({400, 30, -400}); p.tick(1.016, &shm);
  CHECK(p.linked());
  CHECK(g.lastPose.pos.x == doctest::Approx(400));
}

TEST_CASE("off the generated map shows a warning") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  g.ground = 30;
  acWrite({0, 10, 0}); g.key = true; p.tick(1.0, &shm);  // car at z=10, GTA ground at 30
  acWrite({0, 10, 0}); p.tick(1.016, &shm);
  CHECK(g.shown("Off the map"));
}

TEST_CASE("F8 again unlinks") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  g.key = true; acWrite({0, 30, 0}); p.tick(1.016, &shm);
  CHECK_FALSE(p.linked());
  CHECK(g.alive.empty());
}
