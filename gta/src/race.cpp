#include "race.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float dist2d(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }
constexpr double kCountdown = 3.0;
constexpr double kResultsSeconds = 12.0;
constexpr double kWaitAfterPlayer = 25.0;     // after you finish, how long the rest get to finish
constexpr double kWaitAfterRacers = 120.0;    // after all AI finished, how long you get
constexpr double kMaxRace = 20 * 60.0;
}  // namespace

std::string raceTime(double s) {
  if (s < 0) return "DNF";
  int m = static_cast<int>(s / 60);
  char b[32];
  std::snprintf(b, sizeof b, "%d:%04.1f", m, s - m * 60);
  return b;
}

std::string Race::carName(int i) const {
  return i < static_cast<int>(opt_.cars.size()) ? opt_.cars[i] : "mixed";
}

void Race::tick(double now) {
  if (w_.keyPressed(RaceWorld::kPolice)) police(now);
  bool raceKey = w_.keyPressed(RaceWorld::kRace);
  switch (state_) {
    case State::Idle:
      if (raceKey) { state_ = State::Menu; item_ = 0; menu(now); }
      break;
    case State::Menu:
      if (raceKey) state_ = State::Idle;
      else menu(now);
      break;
    case State::Countdown:
    case State::Racing:
      if (raceKey) { end(true); say("Race cancelled", now); break; }
      racing(now);
      break;
    case State::Results:
      if (raceKey || now > resultsUntil_) { end(false); break; }
      racing(now);
      break;
  }
  if (now < msgUntil_) w_.text(0.5f, 0.2f, msg_);
}

void Race::police(double now) {
  int stars = w_.wanted();
  int next = stars == 0 ? 3 : stars < 4 ? 4 : 0;
  w_.setWanted(next);
  say(next ? "Police chase: " + std::to_string(next) + " stars (" + opt_.policeKey + " for more / to end)"
           : "Police chase over", now);
}

void Race::menu(double now) {
  w_.blockPhone();
  int nCars = static_cast<int>(opt_.cars.size()) + 1;  // + mixed
  if (w_.keyPressed(RaceWorld::kUp) || w_.keyPressed(RaceWorld::kDown)) item_ = 1 - item_;
  int d = (w_.keyPressed(RaceWorld::kRight) ? 1 : 0) - (w_.keyPressed(RaceWorld::kLeft) ? 1 : 0);
  if (d && item_ == 0) opt_.opponents = std::clamp(opt_.opponents + d, 1, 7);
  if (d && item_ == 1) car_ = (car_ + d + nCars) % nCars;
  Vec3 wp;
  bool haveWp = w_.waypoint(wp);
  char line[96];
  w_.text(0.5f, 0.30f, "RACE TO THE MAP WAYPOINT", 0.6f);
  std::snprintf(line, sizeof line, "%s Opponents:  < %d >", item_ == 0 ? ">" : " ", opt_.opponents);
  w_.text(0.5f, 0.36f, line);
  std::snprintf(line, sizeof line, "%s Their car:  < %s >", item_ == 1 ? ">" : " ", carName(car_).c_str());
  w_.text(0.5f, 0.40f, line);
  if (haveWp) {
    std::snprintf(line, sizeof line, "Finish: %.1f km away (straight line)", dist2d(wp, w_.playerPos()) / 1000.f);
    w_.text(0.5f, 0.44f, line);
  } else {
    w_.text(0.5f, 0.44f, "No waypoint: set one on the map first (Esc, Map)");
  }
  w_.text(0.5f, 0.49f, "Arrows: choose   Enter: start   " + opt_.raceKey + ": close", 0.35f);
  if (w_.keyPressed(RaceWorld::kEnter)) {
    if (!haveWp) { say("Set a waypoint on the map first", now); return; }
    if (dist2d(wp, w_.playerPos()) < 100.f) { say("The waypoint is too close", now); return; }
    finish_ = wp;
    start(now);
  }
}

void Race::start(double now) {
  Vec3 p = w_.playerPos();
  float h = w_.playerHeadingDeg() * 3.14159265f / 180.f;
  Vec3 back{std::sin(h), -std::cos(h), 0};  // GTA forward is (-sin h, cos h)
  racers_.clear();
  for (int i = 0; i < opt_.opponents; i++) {
    std::string model = car_ < static_cast<int>(opt_.cars.size()) ? opt_.cars[car_] : opt_.cars[i % opt_.cars.size()];
    Vec3 at{p.x + back.x * 8.f * (i + 1), p.y + back.y * 8.f * (i + 1), p.z};  // single file behind you
    float gz;
    at.z = w_.groundZ({at.x, at.y, p.z + 4.f}, gz) ? gz + 0.5f : p.z + 0.5f;
    int car = w_.spawnRacer(model, at, w_.playerHeadingDeg());
    if (car) racers_.push_back({car, model});
  }
  if (racers_.empty()) { say("Couldn't make any opponent cars (check race_cars in gta_link.ini)", now); state_ = State::Idle; return; }
  w_.finishPoint(finish_, true);
  raceKm_ = dist2d(p, finish_) / 1000.f;
  startAt_ = now + kCountdown;
  playerFinish_ = -1;
  state_ = State::Countdown;
}

void Race::racing(double now) {
  w_.drawFinish(finish_);
  if (state_ == State::Countdown) {
    double left = startAt_ - now;
    if (left > 0) {
      w_.text(0.5f, 0.35f, std::to_string(static_cast<int>(std::ceil(left))), 1.2f);
      return;
    }
    for (auto& r : racers_) w_.startRacer(r.car, finish_, opt_.maxSpeed, opt_.drivingStyle);
    state_ = State::Racing;
  }
  double t = now - startAt_;
  if (t < 1.0) w_.text(0.5f, 0.35f, "GO!", 1.2f);

  Vec3 me = w_.playerPos();
  if (playerFinish_ < 0 && dist2d(me, finish_) < opt_.finishRadius) playerFinish_ = t;
  double lastAi = -1;
  bool aiDone = true;
  for (auto& r : racers_) {
    if (r.finishedAt == -1) {
      if (!w_.racerOk(r.car)) r.finishedAt = -2;  // wrecked or gone: DNF
      else if (dist2d(w_.racerPos(r.car), finish_) < opt_.finishRadius) r.finishedAt = t;
    }
    if (r.finishedAt == -1) aiDone = false;
    lastAi = std::max(lastAi, r.finishedAt);
  }

  // standings: finished by time, then by distance left
  struct Entry { std::string name; double time; float left; bool me; };
  std::vector<Entry> e{{"You", playerFinish_, dist2d(me, finish_), true}};
  for (auto& r : racers_)
    e.push_back({r.model, r.finishedAt >= 0 ? r.finishedAt : -1,
                 r.finishedAt == -2 ? 1e9f : (w_.racerOk(r.car) ? dist2d(w_.racerPos(r.car), finish_) : 1e9f), false});
  std::stable_sort(e.begin(), e.end(), [](const Entry& a, const Entry& b) {
    bool fa = a.time >= 0, fb = b.time >= 0;
    if (fa != fb) return fa;
    return fa ? a.time < b.time : a.left < b.left;
  });
  int pos = 1;
  for (size_t i = 0; i < e.size(); i++) if (e[i].me) pos = static_cast<int>(i) + 1;

  char line[96];
  if (state_ == State::Racing) {
    std::snprintf(line, sizeof line, "Position %d/%zu   %s   %.1f km to go", pos, e.size(),
                  raceTime(playerFinish_ >= 0 ? playerFinish_ : t).c_str(), dist2d(me, finish_) / 1000.f);
    w_.text(0.5f, 0.06f, line, 0.5f);
    bool over = (playerFinish_ >= 0 && (aiDone || t - playerFinish_ > kWaitAfterPlayer)) ||
                (aiDone && playerFinish_ < 0 && t - lastAi > kWaitAfterRacers) || t > kMaxRace;
    if (over) {
      state_ = State::Results;
      resultsUntil_ = now + kResultsSeconds;
      w_.finishPoint(finish_, false);
      char head[96];
      std::snprintf(head, sizeof head, "race %.1f km to (%.0f, %.0f), %zu opponents:", raceKm_, finish_.x, finish_.y,
                    racers_.size());
      std::string all = head;
      for (size_t i = 0; i < e.size(); i++)
        all += "  " + std::to_string(i + 1) + ". " + e[i].name + " " + raceTime(e[i].time);
      w_.saveResults(all);
    }
  }
  if (state_ == State::Results) {
    w_.text(0.5f, 0.28f, "RESULTS", 0.7f);
    w_.text(0.5f, 0.34f + 0.04f * static_cast<float>(std::min<size_t>(e.size(), 8)) + 0.02f,
            "saved to gta_race_results.txt   (" + opt_.raceKey + " closes)", 0.35f);
    for (size_t i = 0; i < e.size() && i < 8; i++) {
      std::snprintf(line, sizeof line, "%zu.  %-10s  %s", i + 1, e[i].name.c_str(), raceTime(e[i].time).c_str());
      w_.text(0.5f, 0.34f + 0.04f * static_cast<float>(i), line);
    }
  }
}

void Race::end(bool remove) {
  for (auto& r : racers_) w_.releaseRacer(r.car, remove);
  racers_.clear();
  w_.finishPoint(finish_, false);
  state_ = State::Idle;
}
