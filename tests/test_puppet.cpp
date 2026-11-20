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
  float originH = 0.55f;
  float steer = 99;
  float gas = -1, brake = -1;
  Quat rot{0, 0, 0, 1};
  std::vector<int> prepared, released;

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
  float originHeight(int) override { return originH; }
  void setSteer(int, float b) override { steer = b; }
  void setPedals(int, float g, float b) override { gas = g; brake = b; }
  Quat rotation(int) override { return rot; }
  Vec3 fwd{0, 1, 0};
  Vec3 forward(int) override { return fwd; }
  void prepare(int v) override { prepared.push_back(v); }
  void release(int v) override { released.push_back(v); }
  bool diag = false;
  bool diagKeyPressed() override { bool k = diag; diag = false; return k; }
  bool reset = false;
  bool resetKeyPressed() override { bool k = reset; reset = false; return k; }
  std::string diagnostics(int) override { return "GTA speed 42 km/h"; }
  std::vector<Vec3> holes;
  void logHole(Vec3 p) override { holes.push_back(p); }
  bool shown(const std::string& s) const { for (auto& t : texts) if (t.find(s) != std::string::npos) return true; return false; }
};

static LinkState shm;
static float acSpeed = 88;
static void acWrite(Vec3 posAc, float rideHeight = 0, float steerDeg = 0, float lockDeg = 0) {
  LinkState s{};
  s.magic = LINK_MAGIC; s.version = LINK_VERSION;
  s.pos[0] = posAc.x; s.pos[1] = posAc.y; s.pos[2] = posAc.z;
  s.fwd[2] = -1; s.up[1] = 1;
  s.speedKmh = acSpeed; s.gear = 4; s.rpm = 4200;
  s.rideHeight = rideHeight;
  s.steerDeg = steerDeg;
  s.steerLockDeg = lockDeg;
  s.teleportAck = shm.teleportAck;  // the app keeps reporting its last teleport
  s.teleportStatus = shm.teleportStatus;
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

TEST_CASE("shows why the shared memory couldn't be opened") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  g.key = true;
  p.tick(1.0, nullptr, "Windows error 5");
  CHECK(g.shown("Windows error 5"));
}

TEST_CASE("shows why AC's data was rejected") {
  shm = {}; FakeGame g; Puppet p(g, "sultan");
  acWrite({0, 30, 0});
  shm.version = 99;
  g.key = true;
  p.tick(1.0, &shm);
  CHECK_FALSE(p.linked());
  CHECK(g.shown("version"));
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

TEST_CASE("lifts the GTA car by the difference in ride height plus trim") {
  shm = {}; FakeGame g; Puppet p(g, "sultan", {.heightTrim = 0.05f});
  g.originH = 0.55f;
  acWrite({0, 30, 0}, 0.30f); g.key = true; p.tick(1.0, &shm);
  acWrite({1, 30, 0}, 0.30f); p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30 + 0.25f + 0.05f));
}

TEST_CASE("without AC ride height only the trim applies") {
  shm = {}; FakeGame g; Puppet p(g, "sultan", {.heightTrim = 0.1f});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({1, 30, 0}); p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30.1f));
}

TEST_CASE("link message shows the height match") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  g.originH = 0.62f;
  acWrite({0, 30, 0}, 0.30f); g.key = true; p.tick(1.0, &shm);
  CHECK(p.message().find("AC 0.30") != std::string::npos);
  CHECK(p.message().find("GTA 0.62") != std::string::npos);
  CHECK(p.message().find("+0.32") != std::string::npos);
}

TEST_CASE("without AC ride height, measures it against GTA's ground at link time") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  g.originH = 0.61f;
  g.ground = 29.85f;  // AC car position is 0.15 m above GTA's road
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  CHECK(p.message().find("AC 0.15") != std::string::npos);
  acWrite({1, 30, 0}); p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30 + 0.46f));
}

TEST_CASE("ground measurement ignores silly values") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  g.originH = 0.61f;
  g.ground = 25.0f;  // 5 m below: probably a gap or bridge, don't trust it
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({1, 30, 0}); p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30));
}

TEST_CASE("if GTA's ground isn't loaded at link time, keeps trying and lifts once it is") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  g.originH = 0.61f;
  g.hasGround = false;
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({0, 30, 0}); p.tick(1.016, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30));  // not measured yet
  g.hasGround = true; g.ground = 29.85f;
  acWrite({0, 30, 0}); p.tick(1.032, &shm);
  acWrite({0, 30, 0}); p.tick(1.048, &shm);
  CHECK(g.lastPose.pos.z == doctest::Approx(30.46f));
  CHECK(p.message().find("lift +0.46") != std::string::npos);
}

TEST_CASE("steers the GTA car's wheels from AC's steering (right in AC = right in GTA)") {
  shm = {}; FakeGame g; Puppet p(g, "zr350", {.steerLead = 0});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({0, 30, 0}, 0, 225, 450); p.tick(1.016, &shm);  // half lock to the right
  CHECK(g.steer == doctest::Approx(-0.5f));
  acWrite({0, 30, 0}, 0, -900, 450); p.tick(1.032, &shm);  // past full lock left: clamped
  CHECK(g.steer == doctest::Approx(1.0f));
}

TEST_CASE("steer_invert flips the direction; unknown lock uses 450 degrees") {
  shm = {}; FakeGame g; Puppet p(g, "zr350", {.steerInvert = true, .steerLead = 0});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({0, 30, 0}, 0, 225, 0); p.tick(1.016, &shm);
  CHECK(g.steer == doctest::Approx(0.5f));
}

TEST_CASE("steer_scale widens or narrows the range, steer_offset moves the centre") {
  shm = {}; FakeGame g; Puppet p(g, "zr350", {.steerScale = 0.5f, .steerOffset = 0.1f, .steerLead = 0});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({0, 30, 0}, 0, 225, 450); p.tick(1.016, &shm);  // half right
  CHECK(g.steer == doctest::Approx(-0.25f + 0.1f));
  acWrite({0, 30, 0}, 0, 0, 450); p.tick(1.032, &shm);
  CHECK(g.steer == doctest::Approx(0.1f));
}

struct LayeredGame : FakeGame {
  // ground at 30 (overpass) and at 20 (road under it); probes return the highest ground below the probe point
  bool groundZ(Vec3 p, float& z) override {
    if (p.z >= 30) { z = 30; return true; }
    if (p.z >= 20) { z = 20; return true; }
    return false;
  }
};

TEST_CASE("driving under an overpass is not 'off the map'") {
  shm = {}; LayeredGame g; Puppet p(g, "zr350");
  acWrite({0, 20.3f, 0}); g.key = true; p.tick(1.0, &shm);  // car on the lower road, z 20.3
  acWrite({0, 20.3f, 0}); p.tick(1.016, &shm);
  CHECK_FALSE(g.shown("Off the map"));
}

TEST_CASE("really under the map is still detected") {
  shm = {}; LayeredGame g; Puppet p(g, "zr350");
  acWrite({0, 10, 0}); g.key = true; p.tick(1.0, &shm);  // 10 m below the lowest ground
  acWrite({0, 10, 0}); p.tick(1.016, &shm);
  CHECK(g.shown("Off the map"));
}

TEST_CASE("AC's throttle and brake go to GTA so its engine revs and brake lights work") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  LinkState s = shm; s.gas = 0.7f; s.brake = 0.2f; seqWrite(&shm, s);
  p.tick(1.016, &shm);
  CHECK(g.gas == doctest::Approx(0.7f));
  CHECK(g.brake == doctest::Approx(0.2f));
}

TEST_CASE("pedals can be switched off") {
  shm = {}; FakeGame g; Puppet p(g, "zr350", {.pedals = false});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  LinkState s = shm; s.gas = 0.7f; seqWrite(&shm, s);
  p.tick(1.016, &shm);
  CHECK(g.gas == doctest::Approx(-1));
}

static LinkCommand cmd;
static void acAck(uint32_t id, int32_t status) {
  LinkState s = shm; s.teleportAck = id; s.teleportStatus = status; seqWrite(&shm, s);
}

TEST_CASE("F8 in a GTA car asks AC to move there, then uses that car") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.alive.push_back(42); g.inVehicle = 42; g.pos = {100, 200, 30}; g.ground = 29.5f;
  acWrite({0, 30, 0});
  g.key = true; p.tick(1.0, &shm, "", &cmd);
  CHECK_FALSE(p.linked());
  REQUIRE(cmd.magic == LINK_MAGIC);
  CHECK(cmd.teleportId == 1);
  CHECK(cmd.pos[0] == doctest::Approx(100));     // AC = (gta.x, gta.z, -gta.y), dropped from 1 m above GTA's ground
  CHECK(cmd.pos[1] == doctest::Approx(30.5f));
  CHECK(cmd.pos[2] == doctest::Approx(-200));
  CHECK(cmd.fwd[2] == doctest::Approx(-1));      // facing GTA north = AC -Z
  CHECK(g.shown("moving the Assetto Corsa car"));
  acAck(1, 1); acWrite({100, 30.5f, -200});
  p.tick(1.1, &shm, "", &cmd);
  REQUIRE(p.linked());
  CHECK(g.spawnedModel.empty());                 // didn't spawn: uses the car you were in
  CHECK(g.prepared == std::vector<int>{42});
  g.key = true; acWrite({100, 30.5f, -200}); p.tick(1.2, &shm, "", &cmd);  // F8 again
  CHECK_FALSE(p.linked());
  CHECK(g.released == std::vector<int>{42});
  CHECK(g.exists(42));                           // your car stays
}

TEST_CASE("if AC can't teleport, you keep your car and it is brought to the AC car") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.alive.push_back(42); g.inVehicle = 42; g.pos = {100, 200, 30};
  acWrite({0, 30, 0});
  g.key = true; p.tick(1.0, &shm, "", &cmd);
  acAck(1, -1); acWrite({0, 30, 0});
  p.tick(1.1, &shm, "", &cmd);
  CHECK(p.linked());
  CHECK(g.spawnedModel.empty());   // no new car: you keep the look you picked
  CHECK(g.prepared == std::vector<int>{42});
  CHECK(p.message().find("can't move") != std::string::npos);
}

TEST_CASE("if AC says moved but the AC car isn't there after 3 s, same: keep your car, say why") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.alive.push_back(42); g.inVehicle = 42; g.pos = {100, 200, 30};
  acWrite({0, 30, 0});
  g.key = true; p.tick(1.0, &shm, "", &cmd);
  acAck(1, 1); acWrite({0, 30, 0}); p.tick(2.0, &shm, "", &cmd);  // "moved", but still far away
  CHECK_FALSE(p.linked());
  acWrite({0, 30, 0}); p.tick(4.1, &shm, "", &cmd);
  CHECK(p.linked());
  CHECK(g.spawnedModel.empty());
  CHECK(p.message().find("didn't move") != std::string::npos);
}

TEST_CASE("an AC car already a few metres away doesn't count as having teleported") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.alive.push_back(42); g.inVehicle = 42; g.pos = {6, 0, 30};
  acWrite({0, 30, 0});  // AC car 6 m from your GTA car, before any teleport
  g.key = true; p.tick(1.0, &shm, "", &cmd);
  acAck(1, 1); acWrite({0, 30, 0}); p.tick(1.1, &shm, "", &cmd);
  CHECK_FALSE(p.linked());  // still waiting for the AC car to really arrive
}

TEST_CASE("link message names the script version and build") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  acWrite({0, 30, 0}, 0.3f); g.key = true; p.tick(1.0, &shm);
  CHECK(p.message().find("[link v") != std::string::npos);
}

TEST_CASE("F7 shows diagnostics while linked") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  g.diag = true; acWrite({0, 30, 0}); g.texts.clear(); p.tick(1.016, &shm);
  CHECK(g.shown("GTA speed 42 km/h"));
  CHECK(g.shown("AC gas"));
  g.diag = true; acWrite({0, 30, 0}); g.texts.clear(); p.tick(1.032, &shm);
  CHECK_FALSE(g.shown("GTA speed"));
}

TEST_CASE("falling through is logged once per fall") {
  shm = {}; FakeGame g; Puppet p(g, "zr350");
  g.ground = 30;
  acWrite({0, 10, 0}); g.key = true; p.tick(1.0, &shm);
  for (int i = 1; i <= 5; i++) { acWrite({float(i), 10, 0}); p.tick(1.0 + i * 0.016, &shm); }
  REQUIRE(g.holes.size() == 1);
  CHECK(g.holes[0].z == doctest::Approx(10));
  acWrite({0, 30.3f, 0}); p.tick(2.0, &shm);       // back on the road (reset)
  acWrite({50, 10, 0}); p.tick(2.1, &shm);         // falls again elsewhere
  CHECK(g.holes.size() == 2);
}

TEST_CASE("F9 puts the AC car back on its wheels at the current spot, facing the way you drove") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.ground = 29.5f;
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm, "", &cmd);
  acWrite({10, 30, -20}); p.tick(1.016, &shm, "", &cmd);
  g.reset = true; acWrite({10, 30, -20}); p.tick(1.032, &shm, "", &cmd);
  REQUIRE(cmd.teleportId == 1);
  CHECK(cmd.pos[0] == doctest::Approx(10));
  CHECK(cmd.pos[1] == doctest::Approx(30.5f));  // GTA ground + 1 m
  CHECK(cmd.pos[2] == doctest::Approx(-20));
  CHECK(cmd.fwd[1] == doctest::Approx(0));      // level
  CHECK(cmd.fwd[2] == doctest::Approx(-1));
  CHECK(p.linked());
}

TEST_CASE("stuck under the map for over a second resets by itself (unless auto_reset is off)") {
  for (int autoReset = 0; autoReset < 2; autoReset++) {
    shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350", {.autoReset = autoReset == 1});
    g.ground = 30;
    acWrite({0, 10, 0}); g.key = true; p.tick(1.0, &shm, "", &cmd);
    for (int i = 1; i <= 80; i++) { acWrite({0, 10, 0}); p.tick(1.0 + i * 0.016, &shm, "", &cmd); }
    CHECK(cmd.teleportId == (autoReset ? 1u : 0u));
    if (autoReset) CHECK(cmd.pos[1] == doctest::Approx(31));  // onto the ground above, + 1 m
  }
}

TEST_CASE("F8 in a car sends the GTA car's own heading (here: facing east), level") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.alive.push_back(42); g.inVehicle = 42; g.pos = {100, 200, 30};
  g.fwd = {0.97f, 0, 0.24f};  // east, nose slightly up
  acWrite({0, 30, 0});
  g.key = true; p.tick(1.0, &shm, "", &cmd);
  CHECK(cmd.fwd[0] == doctest::Approx(1).epsilon(0.01));  // GTA east = AC +X
  CHECK(cmd.fwd[1] == doctest::Approx(0));                // level
  CHECK(cmd.fwd[2] == doctest::Approx(0));
}

TEST_CASE("steer_lead feeds GTA where the steering will be, to hide GTA's smoothing") {
  shm = {}; FakeGame g; Puppet p(g, "zr350", {.steerLead = 0.1f});
  acWrite({0, 30, 0}); g.key = true; p.tick(1.0, &shm);
  acWrite({0, 30, 0}, 0, 0, 450); p.tick(1.0, &shm);
  acWrite({0, 30, 0}, 0, 45, 450); p.tick(1.05, &shm);  // turning right at 900 deg/s
  // now -45/450 = -0.1, plus 0.1 s ahead at -2.0/s = -0.2 more
  CHECK(g.steer == doctest::Approx(-0.3f).epsilon(0.02));
  acWrite({0, 30, 0}, 0, 45, 450); p.tick(1.10, &shm);  // holding still: no lead
  CHECK(g.steer == doctest::Approx(-0.1f).epsilon(0.02));
}

TEST_CASE("sunk into the road and not moving for 1 s resets onto the road (unless auto_reset is off)") {
  for (int autoReset = 0; autoReset < 2; autoReset++) {
    shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350", {.autoReset = autoReset == 1});
    g.ground = 30; acSpeed = 0;
    acWrite({0, 29.6f, 0}); g.key = true; p.tick(1.0, &shm, "", &cmd);  // car centre 0.4 m under GTA's road
    for (int i = 1; i <= 70; i++) { acWrite({0, 29.6f, 0}); p.tick(1.0 + i * 0.016, &shm, "", &cmd); }
    CHECK(cmd.teleportId == (autoReset ? 1u : 0u));
    if (autoReset) CHECK(cmd.pos[1] == doctest::Approx(31));  // on top of the road, + 1 m
    acSpeed = 88;
  }
}

TEST_CASE("driving normally on the road is not 'sunk'") {
  shm = {}; cmd = {}; FakeGame g; Puppet p(g, "zr350");
  g.ground = 30; acSpeed = 0;
  acWrite({0, 30.4f, 0}); g.key = true; p.tick(1.0, &shm, "", &cmd);
  for (int i = 1; i <= 200; i++) { acWrite({0, 30.4f, 0}); p.tick(1.0 + i * 0.016, &shm, "", &cmd); }
  CHECK(cmd.teleportId == 0u);
  acSpeed = 88;
}
