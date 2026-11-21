#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <map>
#include <set>
#include "race.hpp"

struct FakeWorld : RaceWorld {
  std::set<Key> keys;
  int stars = 0;
  bool haveWp = true;
  Vec3 wp{1000, 0, 0}, me{0, 0, 30};
  float heading = 270;  // facing east (+x)
  int next = 100;
  std::map<int, Vec3> cars;
  std::map<int, std::string> models;
  std::map<int, Vec3> dest;
  std::map<int, float> speed, power;
  std::set<int> driving, removed, released, gone, moved;
  bool blip = false, phoneBlocked = false;
  Vec3 blipAt{};
  std::vector<std::string> texts, saved;
  std::vector<Route> stored;
  bool keyPressed(Key k) override { return keys.erase(k) > 0; }
  void blockPhone() override { phoneBlocked = true; }
  int wanted() override { return stars; }
  void setWanted(int s) override { stars = s; }
  bool waypoint(Vec3& o) override { o = wp; return haveWp; }
  Vec3 playerPos() override { return me; }
  float playerHeadingDeg() override { return heading; }
  bool groundZ(Vec3, float& z) override { z = 30; return true; }
  float approxHeight(float, float) override { return 120; }  // a hill
  bool roadNode(Vec3 n, Vec3& o, float& h) override { o = {n.x + 3, n.y, 118}; h = 270; return true; }
  int spawnRacer(const std::string& m, Vec3 p, float) override { cars[next] = p; models[next] = m; speed[next] = 0; return next++; }
  void driveRacer(int c, Vec3 d, float, int) override { driving.insert(c); dest[c] = d; }
  void setRacerPower(int c, float m) override { power[c] = m; }
  void moveRacer(int c, Vec3 p, float) override { moved.insert(c); cars[c] = p; }
  bool racerOk(int c) override { return !gone.count(c); }
  Vec3 racerPos(int c) override { return cars[c]; }
  float racerSpeed(int c) override { return speed[c]; }
  void releaseRacer(int c, bool remove) override { (remove ? removed : released).insert(c); }
  void target(Vec3 p, bool show) override { blip = show; blipAt = p; }
  void drawCheckpoint(Vec3, bool) override {}
  void text(float, float, const std::string& s, float) override { texts.push_back(s); }
  void saveResults(const std::string& s) override { saved.push_back(s); }
  std::vector<Route> loadRoutes() override { return stored; }
  void saveRoutes(const std::vector<Route>& r) override { stored = r; }
  bool shown(const std::string& s) const { for (auto& t : texts) if (t.find(s) != std::string::npos) return true; return false; }
  void press(Race& r, Key k, double t) { keys = {k}; r.tick(t); }
};

TEST_CASE("F10 cycles the police: none -> 3 stars -> 4 stars -> off") {
  FakeWorld w; Race r(w);
  w.keys = {RaceWorld::kPolice}; r.tick(1); CHECK(w.stars == 3);
  w.keys = {RaceWorld::kPolice}; r.tick(2); CHECK(w.stars == 4);
  w.keys = {RaceWorld::kPolice}; r.tick(3); CHECK(w.stars == 0);
  w.stars = 1;  // a wanted level GTA gave you
  w.keys = {RaceWorld::kPolice}; r.tick(4); CHECK(w.stars == 4);
}

TEST_CASE("menu: choose 2 opponents in futos, race behind you, countdown, then they drive") {
  FakeWorld w; Race r(w, {.opponents = 3, .cars = {"elegy2", "futo"}});
  w.keys = {RaceWorld::kRace}; r.tick(1);
  REQUIRE(r.state() == Race::State::Menu);
  CHECK(w.phoneBlocked);
  w.keys = {RaceWorld::kDown}; r.tick(1.05);                      // route -> opponents
  w.keys = {RaceWorld::kLeft}; r.tick(1.1);                       // 3 -> 2 opponents
  w.keys = {RaceWorld::kDown}; r.tick(1.2);
  w.keys = {RaceWorld::kRight}; r.tick(1.3);                      // car: elegy2 -> futo
  w.keys = {RaceWorld::kEnter}; r.tick(1.4);
  REQUIRE(r.state() == Race::State::Countdown);
  REQUIRE(r.racerCount() == 2);
  for (auto& [c, m] : w.models) CHECK(m == "futo");
  for (auto& [c, p] : w.cars) CHECK(p.x < -1);                   // behind you (you face east)
  CHECK(w.blip);
  r.tick(2.0); CHECK(w.driving.empty());                          // still counting down
  r.tick(4.5); CHECK(r.state() == Race::State::Racing); CHECK(w.driving.size() == 2);
  // the waypoint has no height: the AI drives to the road at the hill's height, not to sea level
  for (auto& [c, d] : w.dest) CHECK(d.z == doctest::Approx(118));
}

TEST_CASE("no waypoint: the race doesn't start") {
  FakeWorld w; w.haveWp = false; Race r(w);
  w.keys = {RaceWorld::kRace}; r.tick(1);
  w.keys = {RaceWorld::kEnter}; r.tick(1.1);
  CHECK(r.state() == Race::State::Menu);
  CHECK(w.shown("waypoint"));
}

TEST_CASE("finishing: positions, results, then the AI drive off") {
  FakeWorld w; Race r(w, {.opponents = 2});
  w.keys = {RaceWorld::kRace}; r.tick(1);
  w.keys = {RaceWorld::kEnter}; r.tick(1.1);
  r.tick(4.2);  // GO
  auto it = w.cars.begin();
  int a = it->first, b = (++it)->first;
  w.cars[a] = {1000, 0, 30};  // AI a finishes first (the finish was snapped to the road at x 1003)
  r.tick(30);
  w.me = {500, 0, 30};
  r.tick(31);
  CHECK(w.shown("Position 2/3"));
  w.me = {1003, 5, 30};  // you finish second
  r.tick(40);
  w.gone.insert(b);      // b got wrecked: DNF
  r.tick(41);
  REQUIRE(r.state() == Race::State::Results);
  CHECK(w.shown("RESULTS"));
  CHECK(w.shown("2.  You"));
  CHECK(w.shown("DNF"));
  REQUIRE(w.saved.size() == 1);
  CHECK(w.saved[0].find("2. You 0:35.9") != std::string::npos);
  CHECK(w.saved[0].find("to the waypoint, 1.0 km") != std::string::npos);
  r.tick(60);  // results shown long enough
  CHECK(r.state() == Race::State::Idle);
  CHECK(w.released.size() == 2);
  CHECK_FALSE(w.blip);
}

TEST_CASE("F11 during a race cancels it and removes the opponents") {
  FakeWorld w; Race r(w, {.opponents = 2});
  w.keys = {RaceWorld::kRace}; r.tick(1);
  w.keys = {RaceWorld::kEnter}; r.tick(1.1);
  r.tick(4.2);
  w.keys = {RaceWorld::kRace}; r.tick(5);
  CHECK(r.state() == Race::State::Idle);
  CHECK(w.removed.size() == 2);
}

TEST_CASE("raceTime") {
  CHECK(raceTime(83.44) == "1:23.4");
  CHECK(raceTime(-1) == "DNF");
}
#include "keys.hpp"
TEST_CASE("key names from gta_link.ini") {
  CHECK(keyCode("F10") == 0x79);
  CHECK(keyCode(" f8 ") == 0x77);
  CHECK(keyCode("NUM5") == 0x65);
  CHECK(keyCode("pageup") == 0x21);
  CHECK(keyCode("K") == 'K');
  CHECK(keyCode("120") == 120);
  CHECK(keyCode("nonsense") == 0);
  CHECK(keyCode("F25") == 0);
}

TEST_CASE("record a route by driving it, then race it through its checkpoints") {
  FakeWorld w; Race r(w, {.opponents = 1, .recordSpacing = 150});
  w.press(r, RaceWorld::kRace, 1);
  w.press(r, RaceWorld::kLeft, 1.1);  // route: waypoint -> record a new route (wraps round)
  CHECK(w.shown("record a new route"));
  w.press(r, RaceWorld::kEnter, 1.2);
  REQUIRE(r.state() == Race::State::Recording);
  // drive east 400 m, then turn north 300 m
  double t = 2;
  for (float x = 0; x <= 400; x += 10) { w.me = {x, 0, 30}; r.tick(t += 0.1); }
  for (float y = 10; y <= 300; y += 10) { w.me = {400, y, 30}; r.tick(t += 0.1); }
  w.press(r, RaceWorld::kEnter, t += 0.1);
  CHECK(r.state() == Race::State::Idle);
  REQUIRE(w.stored.size() == 1);
  const Route& rt = w.stored[0];
  CHECK(rt.name.find("Route 1 (0.7 km)") == 0);
  CHECK(rt.pts.front().x == doctest::Approx(0));
  CHECK(rt.pts.back().y > 280);
  bool cornerKept = false;
  for (auto& p : rt.pts) if (p.x > 390 && p.y < 25) cornerKept = true;  // a checkpoint near the corner
  CHECK(cornerKept);

  // racing it from far away: told to drive to the start
  w.press(r, RaceWorld::kRace, t += 1);
  CHECK(w.shown("Route 1"));  // the new route is selected
  w.press(r, RaceWorld::kEnter, t += 0.1);
  CHECK(r.state() == Race::State::Idle);
  CHECK(w.shown("Drive to the start"));
  w.me = {5, 0, 30};
  w.press(r, RaceWorld::kRace, t += 1);
  w.press(r, RaceWorld::kEnter, t += 0.1);
  REQUIRE(r.state() == Race::State::Countdown);
  r.tick(t += 3.5);
  REQUIRE(r.state() == Race::State::Racing);
  int ai = w.cars.begin()->first;
  Vec3 firstCp = w.dest[ai];
  CHECK(firstCp.x == doctest::Approx(rt.pts[1].x));
  w.cars[ai] = rt.pts[1];  // AI reaches checkpoint 1: on to checkpoint 2
  r.tick(t += 0.1);
  CHECK(w.dest[ai].x == doctest::Approx(rt.pts[2].x));
  CHECK(w.dest[ai].y == doctest::Approx(rt.pts[2].y));
  // you skip checkpoint 1 and drive to the finish: not finished (checkpoints count)
  w.me = rt.pts.back();
  r.tick(t += 0.1);
  CHECK(w.shown("checkpoint 1/"));
}

TEST_CASE("a stuck opponent is told to drive on, then put back on the road") {
  FakeWorld w; Race r(w, {.opponents = 1});
  w.press(r, RaceWorld::kRace, 1);
  w.press(r, RaceWorld::kEnter, 1.1);
  r.tick(4.2);
  int ai = w.cars.begin()->first;
  w.speed[ai] = 0.3f;  // not moving
  double t = 10;
  for (int i = 0; i < 120; i++) r.tick(t += 0.1);  // 12 s stuck
  CHECK(w.moved.count(ai) == 1);
}

TEST_CASE("opponents far behind get extra power, back to normal when close") {
  FakeWorld w; Race r(w, {.opponents = 1});
  w.press(r, RaceWorld::kRace, 1);
  w.press(r, RaceWorld::kEnter, 1.1);
  r.tick(4.2);
  int ai = w.cars.begin()->first;
  w.speed[ai] = 30;
  w.me = {900, 0, 30};
  w.cars[ai] = {0, 0, 30};  // 900 m behind
  r.tick(10);
  CHECK(w.power[ai] > 1.5f);
  w.cars[ai] = {880, 0, 30};
  r.tick(12);
  CHECK(w.power[ai] == doctest::Approx(1));
}

TEST_CASE("routes file round trip") {
  std::vector<Route> a{{"Route 1 (2.0 km)", {{1, 2, 3}, {4.5f, -6, 7}}}, {"Hill", {{0, 0, 0}, {10, 10, 100}, {20, 0, 50}}}};
  auto b = routesFromText(routesToText(a));
  REQUIRE(b.size() == 2);
  CHECK(b[0].name == "Route 1 (2.0 km)");
  CHECK(b[1].pts[1].z == doctest::Approx(100));
  CHECK(routesFromText("junk\n|1,2,3\nX|1,2,3\n").empty());  // no name, or fewer than 2 points
}
