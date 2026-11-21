// Police chase key and street races against GTA AI drivers.
//   F10: police chase: no chase -> 3 stars -> 4 stars -> off
//   F11: race menu: route (to the map waypoint, a saved route, or record a new one by driving it), opponents,
//        their car; Enter starts; F11 during a race (or recording) cancels it
// Routes are checkpoints dropped while you drive, so a race follows the roads you took (curvy ones too), not GTA's
// fastest way. The GTA side is behind RaceWorld so the logic is testable without the game.
#pragma once
#include <string>
#include <vector>
#include "link_math.hpp"

struct Route {
  std::string name;
  std::vector<Vec3> pts;  // pts[0] = start; the rest are checkpoints, the last one is the finish
};

class RaceWorld {
 public:
  virtual ~RaceWorld() = default;
  enum Key { kPolice, kRace, kUp, kDown, kLeft, kRight, kEnter };
  virtual bool keyPressed(Key k) = 0;           // edge-triggered
  virtual void blockPhone() = 0;                // call every frame the menu is open (arrow keys open GTA's phone)
  virtual int wanted() = 0;
  virtual void setWanted(int stars) = 0;        // 0 clears
  virtual bool waypoint(Vec3& out) = 0;         // the map waypoint (its height is unknown), false if none
  virtual Vec3 playerPos() = 0;
  virtual float playerHeadingDeg() = 0;
  virtual bool groundZ(Vec3 above, float& z) = 0;
  virtual float approxHeight(float x, float y) = 0;  // terrain height anywhere on the map (rough)
  virtual bool roadNode(Vec3 near, Vec3& out, float& headingDeg) = 0;  // closest road point for cars
  // A car of `model` with an AI driver, frozen in place; 0 if it couldn't be made.
  virtual int spawnRacer(const std::string& model, Vec3 pos, float headingDeg) = 0;
  virtual void driveRacer(int car, Vec3 dest, float maxSpeed, int drivingStyle) = 0;  // unfreeze, drive there
  virtual void setRacerPower(int car, float mult) = 0;  // 1 = the car's own power (catch-up when far behind)
  virtual void moveRacer(int car, Vec3 pos, float headingDeg) = 0;  // put a stuck car back on the road
  virtual bool racerOk(int car) = 0;            // car and driver still there
  virtual Vec3 racerPos(int car) = 0;
  virtual float racerSpeed(int car) = 0;        // m/s
  virtual void releaseRacer(int car, bool remove) = 0;  // remove: delete now; else drive off and despawn later
  virtual void target(Vec3 pos, bool show) = 0;  // your next checkpoint: map blip with route; show=false removes it
  virtual void drawCheckpoint(Vec3 pos, bool finish) = 0;  // marker in the world, every frame
  virtual void text(float x, float y, const std::string& s, float scale = 0.45f) = 0;
  virtual void saveResults(const std::string& oneLine) = 0;  // appended to the results file with the date
  virtual std::vector<Route> loadRoutes() = 0;
  virtual void saveRoutes(const std::vector<Route>& routes) = 0;
};

struct RaceOptions {
  int opponents = 3;                   // 1..7
  std::vector<std::string> cars{"elegy2", "futo", "sultan", "jester", "zr350", "comet2", "banshee", "dominator"};
  float maxSpeed = 80.f;               // m/s the AI may go
  // GTA driving style flags: avoid cars/people/objects by steering round them, overtake on the wrong side, take the
  // shortest path; never stops for cars in front or for red lights.
  int drivingStyle = 787004;
  float finishRadius = 15.f;
  float checkpointRadius = 30.f;       // passing a checkpoint
  float recordSpacing = 150.f;         // metres between recorded checkpoints (closer at turns)
  float catchUp = 0.6f;                // up to +60% power for opponents far behind you (0 = off)
  std::string policeKey = "F10", raceKey = "F11";  // names for the on-screen hints
};

class Race {
 public:
  Race(RaceWorld& w, RaceOptions o = {}) : w_(w), opt_(std::move(o)) {}
  void tick(double now);

  enum class State { Idle, Menu, Recording, Countdown, Racing, Results };
  State state() const { return state_; }
  size_t racerCount() const { return racers_.size(); }

 private:
  struct Racer {
    int car;
    std::string model;
    size_t next = 0;           // index into course_ of the checkpoint it's driving to
    double finishedAt = -1;    // -1 racing, -2 DNF, else seconds
    double slowSince = -1;     // stuck detection
    int unstuck = 0;
    float power = 1;
  };
  void police(double now);
  void menu(double now);
  void record(double now);
  void start(double now);
  void racing(double now);
  void end(bool remove);
  float remaining(Vec3 pos, size_t next) const;  // metres along the course to the finish
  std::string carName(int i) const;  // menu choice i: a car from the list, or "mixed"
  std::string routeName(int i) const;
  void say(const std::string& s, double now) { msg_ = s; msgUntil_ = now + 3.5; }

  RaceWorld& w_;
  RaceOptions opt_;
  State state_ = State::Idle;
  int item_ = 0;                // menu line: 0 route, 1 opponents, 2 car
  int car_ = 0;                 // index into cars, cars.size() = mixed
  int route_ = 0;               // 0 = map waypoint, 1..n = saved routes, n+1 = record a new one
  std::vector<Route> routes_;
  bool routesLoaded_ = false;
  Route recording_;
  Vec3 recPrev_{};
  float recDir_ = 0;
  bool recDirOk_ = false;
  std::string courseName_;
  std::vector<Vec3> course_;    // checkpoints to drive through, last = finish
  std::vector<float> after_;    // after_[i]: metres from checkpoint i to the finish
  size_t myNext_ = 0;
  std::vector<Racer> racers_;
  double startAt_ = 0, playerFinish_ = -1, resultsUntil_ = 0, lastTune_ = 0;
  float raceKm_ = 0;
  std::string msg_;
  double msgUntil_ = 0;
};

std::string raceTime(double seconds);  // 83.4 -> "1:23.4"
// Routes file: one route per line, "name|x,y,z;x,y,z;..." (GTA coordinates).
std::string routesToText(const std::vector<Route>& routes);
std::vector<Route> routesFromText(const std::string& text);
