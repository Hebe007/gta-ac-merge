// Police chase key and street races against GTA AI drivers, to the waypoint you set on GTA's map.
//   F10: police chase: no chase -> 3 stars -> 4 stars -> off
//   F11: race menu (opponents, their car); Enter starts; F11 during a race cancels it
// The GTA side is behind RaceWorld so the logic is testable without the game.
#pragma once
#include <string>
#include <vector>
#include "link_math.hpp"

class RaceWorld {
 public:
  virtual ~RaceWorld() = default;
  enum Key { kPolice, kRace, kUp, kDown, kLeft, kRight, kEnter };
  virtual bool keyPressed(Key k) = 0;           // edge-triggered
  virtual void blockPhone() = 0;                // call every frame the menu is open (arrow keys open GTA's phone)
  virtual int wanted() = 0;
  virtual void setWanted(int stars) = 0;        // 0 clears
  virtual bool waypoint(Vec3& out) = 0;         // the map waypoint, false if none
  virtual Vec3 playerPos() = 0;
  virtual float playerHeadingDeg() = 0;
  virtual bool groundZ(Vec3 above, float& z) = 0;
  // A car of `model` with an AI driver, frozen in place; 0 if it couldn't be made.
  virtual int spawnRacer(const std::string& model, Vec3 pos, float headingDeg) = 0;
  virtual void startRacer(int car, Vec3 dest, float maxSpeed, int drivingStyle) = 0;  // unfreeze and race there
  virtual bool racerOk(int car) = 0;            // car and driver still there
  virtual Vec3 racerPos(int car) = 0;
  virtual void releaseRacer(int car, bool remove) = 0;  // remove: delete now; else drive off and despawn later
  virtual void finishPoint(Vec3 pos, bool show) = 0;    // map blip with route; show=false removes it
  virtual void drawFinish(Vec3 pos) = 0;                // marker in the world, every frame
  virtual void text(float x, float y, const std::string& s, float scale = 0.45f) = 0;
};

struct RaceOptions {
  int opponents = 3;                   // 1..7
  std::vector<std::string> cars{"elegy2", "futo", "sultan", "jester", "zr350", "comet2", "banshee", "dominator"};
  float maxSpeed = 80.f;               // m/s the AI may go
  int drivingStyle = 1074528293;       // GTA driving style flags: "rushed" (overtakes, runs lights)
  float finishRadius = 15.f;
};

class Race {
 public:
  Race(RaceWorld& w, RaceOptions o = {}) : w_(w), opt_(std::move(o)) {}
  void tick(double now);

  enum class State { Idle, Menu, Countdown, Racing, Results };
  State state() const { return state_; }
  size_t racerCount() const { return racers_.size(); }

 private:
  struct Racer { int car; std::string model; double finishedAt = -1; };
  void police(double now);
  void menu(double now);
  void start(double now);
  void racing(double now);
  void end(bool remove);
  std::string carName(int i) const;  // menu choice i: a car from the list, or "mixed"
  void say(const std::string& s, double now) { msg_ = s; msgUntil_ = now + 3; }

  RaceWorld& w_;
  RaceOptions opt_;
  State state_ = State::Idle;
  int item_ = 0;                // menu line: 0 opponents, 1 car
  int car_ = 0;                 // index into cars, cars.size() = mixed
  Vec3 finish_{};
  std::vector<Racer> racers_;
  double startAt_ = 0, playerFinish_ = -1, resultsUntil_ = 0;
  std::string msg_;
  double msgUntil_ = 0;
};

std::string raceTime(double seconds);  // 83.4 -> "1:23.4"
