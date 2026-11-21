#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <map>
#include <set>
#include "race.hpp"

struct FakeWorld : RaceWorld {
  std::set<Key> keys;
  int stars = 0;
  bool haveWp = true;
  Vec3 wp{1000, 0, 30}, me{0, 0, 30};
  int next = 100;
  std::map<int, Vec3> cars;
  std::map<int, std::string> models;
  std::set<int> started, removed, released, gone;
  bool blip = false, phoneBlocked = false;
  std::vector<std::string> texts;
  bool keyPressed(Key k) override { return keys.erase(k) > 0; }
  void blockPhone() override { phoneBlocked = true; }
  int wanted() override { return stars; }
  void setWanted(int s) override { stars = s; }
  bool waypoint(Vec3& o) override { o = wp; return haveWp; }
  Vec3 playerPos() override { return me; }
  float playerHeadingDeg() override { return 270; }  // facing east (+x)
  bool groundZ(Vec3, float& z) override { z = 30; return true; }
  int spawnRacer(const std::string& m, Vec3 p, float) override { cars[next] = p; models[next] = m; return next++; }
  void startRacer(int c, Vec3, float, int) override { started.insert(c); }
  bool racerOk(int c) override { return !gone.count(c); }
  Vec3 racerPos(int c) override { return cars[c]; }
  void releaseRacer(int c, bool remove) override { (remove ? removed : released).insert(c); }
  void finishPoint(Vec3, bool show) override { blip = show; }
  void drawFinish(Vec3) override {}
  void text(float, float, const std::string& s, float) override { texts.push_back(s); }
  std::vector<std::string> saved;
  void saveResults(const std::string& s) override { saved.push_back(s); }
  bool shown(const std::string& s) const { for (auto& t : texts) if (t.find(s) != std::string::npos) return true; return false; }
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
  w.keys = {RaceWorld::kLeft}; r.tick(1.1);                       // 3 -> 2 opponents
  w.keys = {RaceWorld::kDown}; r.tick(1.2);
  w.keys = {RaceWorld::kRight}; r.tick(1.3);                      // car: elegy2 -> futo
  w.keys = {RaceWorld::kEnter}; r.tick(1.4);
  REQUIRE(r.state() == Race::State::Countdown);
  REQUIRE(r.racerCount() == 2);
  for (auto& [c, m] : w.models) CHECK(m == "futo");
  for (auto& [c, p] : w.cars) CHECK(p.x < -1);                   // behind you (you face east)
  CHECK(w.blip);
  r.tick(2.0); CHECK(w.started.empty());                          // still counting down
  r.tick(4.5); CHECK(r.state() == Race::State::Racing); CHECK(w.started.size() == 2);
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
  w.cars[a] = {995, 0, 30};  // AI a finishes first
  r.tick(30);
  w.me = {500, 0, 30};
  r.tick(31);
  CHECK(w.shown("Position 2/3"));
  w.me = {1000, 5, 30};  // you finish second
  r.tick(40);
  w.gone.insert(b);      // b got wrecked: DNF
  r.tick(41);
  REQUIRE(r.state() == Race::State::Results);
  CHECK(w.shown("RESULTS"));
  CHECK(w.shown("2.  You"));
  CHECK(w.shown("DNF"));
  REQUIRE(w.saved.size() == 1);
  CHECK(w.saved[0].find("2. You 0:35.9") != std::string::npos);
  CHECK(w.saved[0].find("1.0 km") != std::string::npos);
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
