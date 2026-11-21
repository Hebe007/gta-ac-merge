#include "race.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
float dist2d(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }
constexpr double kCountdown = 3.0;
constexpr double kResultsSeconds = 12.0;
constexpr double kWaitAfterPlayer = 25.0;     // after you finish, how long the rest get to finish
constexpr double kWaitAfterRacers = 120.0;    // after all AI finished, how long you get
constexpr double kMaxRace = 30 * 60.0;
constexpr float kStartRadius = 60.f;          // how close to a route's start you must be
constexpr float kRecordTurnDeg = 40.f;        // a turn this sharp drops a checkpoint early
constexpr float kRecordMinGap = 30.f;
}  // namespace

std::string raceTime(double s) {
  if (s < 0) return "DNF";
  int m = static_cast<int>(s / 60);
  char b[32];
  std::snprintf(b, sizeof b, "%d:%04.1f", m, s - m * 60);
  return b;
}

std::string routesToText(const std::vector<Route>& routes) {
  std::string out;
  char b[64];
  for (auto& r : routes) {
    out += r.name + "|";
    for (size_t i = 0; i < r.pts.size(); i++) {
      std::snprintf(b, sizeof b, "%s%.1f,%.1f,%.1f", i ? ";" : "", r.pts[i].x, r.pts[i].y, r.pts[i].z);
      out += b;
    }
    out += "\n";
  }
  return out;
}

std::vector<Route> routesFromText(const std::string& text) {
  std::vector<Route> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t bar = line.find('|');
    if (bar == std::string::npos || bar == 0) continue;
    Route r{line.substr(0, bar), {}};
    std::istringstream pts(line.substr(bar + 1));
    std::string p;
    while (std::getline(pts, p, ';')) {
      Vec3 v{};
      if (std::sscanf(p.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z) == 3) r.pts.push_back(v);
    }
    if (r.pts.size() >= 2) out.push_back(r);
  }
  return out;
}

std::string Race::carName(int i) const {
  return i < static_cast<int>(opt_.cars.size()) ? opt_.cars[i] : "mixed";
}

std::string Race::routeName(int i) const {
  if (i == 0) return "to the map waypoint";
  if (i <= static_cast<int>(routes_.size())) return routes_[i - 1].name;
  return "record a new route";
}

void Race::tick(double now) {
  if (w_.keyPressed(RaceWorld::kPolice)) police(now);
  bool raceKey = w_.keyPressed(RaceWorld::kRace);
  switch (state_) {
    case State::Idle:
      if (raceKey) {
        if (!routesLoaded_) { routes_ = w_.loadRoutes(); routesLoaded_ = true; }
        state_ = State::Menu;
        item_ = 0;
        menu(now);
      }
      break;
    case State::Menu:
      if (raceKey) state_ = State::Idle;
      else menu(now);
      break;
    case State::Recording:
      if (raceKey) { state_ = State::Idle; say("Recording cancelled", now); break; }
      record(now);
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
  int nCars = static_cast<int>(opt_.cars.size()) + 1;      // + mixed
  int nRoutes = static_cast<int>(routes_.size()) + 2;      // + waypoint, + record
  if (w_.keyPressed(RaceWorld::kUp)) item_ = (item_ + 2) % 3;
  if (w_.keyPressed(RaceWorld::kDown)) item_ = (item_ + 1) % 3;
  int d = (w_.keyPressed(RaceWorld::kRight) ? 1 : 0) - (w_.keyPressed(RaceWorld::kLeft) ? 1 : 0);
  if (d && item_ == 0) route_ = (route_ + d + nRoutes) % nRoutes;
  if (d && item_ == 1) opt_.opponents = std::clamp(opt_.opponents + d, 1, 7);
  if (d && item_ == 2) car_ = (car_ + d + nCars) % nCars;
  bool recordNew = route_ == nRoutes - 1;
  Vec3 me = w_.playerPos(), wp;
  bool haveWp = w_.waypoint(wp);
  char line[128];
  w_.text(0.5f, 0.28f, "STREET RACE", 0.6f);
  std::snprintf(line, sizeof line, "%s Route:  < %s >", item_ == 0 ? ">" : " ", routeName(route_).c_str());
  w_.text(0.5f, 0.34f, line);
  std::snprintf(line, sizeof line, "%s Opponents:  < %d >", item_ == 1 ? ">" : " ", opt_.opponents);
  w_.text(0.5f, 0.38f, line);
  std::snprintf(line, sizeof line, "%s Their car:  < %s >", item_ == 2 ? ">" : " ", carName(car_).c_str());
  w_.text(0.5f, 0.42f, line);
  std::string info;
  if (recordNew) {
    info = "Enter, then drive the route; Enter again at the finish saves it";
  } else if (route_ == 0) {
    if (haveWp) { std::snprintf(line, sizeof line, "Finish: %.1f km away (straight line)", dist2d(wp, me) / 1000.f); info = line; }
    else info = "No waypoint: set one on the map first (Esc, Map)";
  } else {
    const Route& r = routes_[route_ - 1];
    std::snprintf(line, sizeof line, "%zu checkpoints; its start is %.0f m from you", r.pts.size() - 1, dist2d(r.pts[0], me));
    info = line;
  }
  w_.text(0.5f, 0.46f, info, 0.4f);
  w_.text(0.5f, 0.51f, "Arrows: choose   Enter: start   " + opt_.raceKey + ": close", 0.35f);
  if (!w_.keyPressed(RaceWorld::kEnter)) return;

  if (recordNew) {
    recording_ = {"", {me}};
    recPrev_ = me;
    recDirOk_ = false;
    state_ = State::Recording;
    return;
  }
  if (route_ == 0) {
    if (!haveWp) { say("Set a waypoint on the map first", now); return; }
    if (dist2d(wp, me) < 100.f) { say("The waypoint is too close", now); return; }
    // the waypoint has no height: take the road there (on hills the AI otherwise aims under the hill)
    Vec3 road;
    float h;
    wp.z = w_.approxHeight(wp.x, wp.y);
    if (w_.roadNode(wp, road, h) && dist2d(road, wp) < 60.f) wp = road;
    courseName_ = "to the waypoint";
    course_ = {wp};
  } else {
    const Route& r = routes_[route_ - 1];
    if (dist2d(r.pts[0], me) > kStartRadius) {
      w_.target(r.pts[0], true);
      state_ = State::Idle;
      say("Drive to the start of " + r.name + " (yellow blip), then " + opt_.raceKey + " again", now);
      return;
    }
    courseName_ = r.name;
    course_.assign(r.pts.begin() + 1, r.pts.end());
  }
  start(now);
}

void Race::record(double now) {
  Vec3 me = w_.playerPos();
  auto& p = recording_.pts;
  float gap = dist2d(me, p.back());
  // a checkpoint every recordSpacing metres, and right where you turn (direction of travel vs. at the last one)
  bool turn = false;
  if (dist2d(me, recPrev_) > 2.f) {
    float dir = std::atan2(me.y - recPrev_.y, me.x - recPrev_.x);
    recPrev_ = me;
    if (!recDirOk_) { recDir_ = dir; recDirOk_ = true; }
    turn = gap > kRecordMinGap && std::fabs(std::remainder(dir - recDir_, 6.2831853f)) * 57.29578f > kRecordTurnDeg;
    if (turn || gap > opt_.recordSpacing) { p.push_back(me); recDir_ = dir; }
  }
  float km = 0;
  for (size_t i = 1; i < p.size(); i++) km += dist2d(p[i - 1], p[i]) / 1000.f;
  km += gap / 1000.f;
  char line[128];
  std::snprintf(line, sizeof line, "RECORDING ROUTE  %.1f km, %zu checkpoints   Enter: save here as the finish   %s: cancel",
                km, p.size() - 1, opt_.raceKey.c_str());
  w_.text(0.5f, 0.06f, line, 0.45f);
  if (!w_.keyPressed(RaceWorld::kEnter)) return;
  if (gap > 20.f) p.push_back(me);
  if (km < 0.3f || p.size() < 2) { say("Too short for a race - keep driving", now); return; }
  char name[64];
  std::snprintf(name, sizeof name, "Route %zu (%.1f km)", routes_.size() + 1, km);
  recording_.name = name;
  routes_.push_back(recording_);
  w_.saveRoutes(routes_);
  route_ = static_cast<int>(routes_.size());  // selected next time
  state_ = State::Idle;
  say(std::string("Saved ") + name + " - drive back to its start and race it with " + opt_.raceKey, now);
}

float Race::remaining(Vec3 pos, size_t next) const {
  if (next >= course_.size()) return 0;
  return dist2d(pos, course_[next]) + after_[next];
}

void Race::start(double now) {
  after_.assign(course_.size(), 0.f);
  for (size_t i = course_.size() - 1; i-- > 0;) after_[i] = after_[i + 1] + dist2d(course_[i], course_[i + 1]);
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
  myNext_ = 0;
  w_.target(course_[0], true);
  raceKm_ = remaining(p, 0) / 1000.f;
  startAt_ = now + kCountdown;
  playerFinish_ = -1;
  lastTune_ = 0;
  state_ = State::Countdown;
}

void Race::racing(double now) {
  bool last = myNext_ + 1 >= course_.size();
  if (myNext_ < course_.size()) w_.drawCheckpoint(course_[myNext_], last);
  if (state_ == State::Countdown) {
    double left = startAt_ - now;
    if (left > 0) {
      w_.text(0.5f, 0.35f, std::to_string(static_cast<int>(std::ceil(left))), 1.2f);
      return;
    }
    for (auto& r : racers_) w_.driveRacer(r.car, course_[0], opt_.maxSpeed, opt_.drivingStyle);
    state_ = State::Racing;
  }
  double t = now - startAt_;
  if (t < 1.0) w_.text(0.5f, 0.35f, "GO!", 1.2f);

  // you: through your next checkpoint
  Vec3 me = w_.playerPos();
  if (playerFinish_ < 0 && myNext_ < course_.size()) {
    float rad = myNext_ + 1 == course_.size() ? opt_.finishRadius : opt_.checkpointRadius;
    if (dist2d(me, course_[myNext_]) < rad) {
      if (++myNext_ >= course_.size()) { playerFinish_ = t; w_.target(course_.back(), false); }
      else w_.target(course_[myNext_], true);
    }
  }
  float myLeft = playerFinish_ >= 0 ? 0.f : remaining(me, myNext_);

  // the AI: next checkpoint, finish, stuck, catch-up
  bool tune = now - lastTune_ > 1.0;
  if (tune) lastTune_ = now;
  double lastAi = -1;
  bool aiDone = true;
  for (auto& r : racers_) {
    if (r.finishedAt == -1) {
      if (!w_.racerOk(r.car)) { r.finishedAt = -2; }  // wrecked or gone: DNF
      else {
        Vec3 rp = w_.racerPos(r.car);
        bool isFinal = r.next + 1 >= course_.size();
        if (dist2d(rp, course_[r.next]) < (isFinal ? opt_.finishRadius : opt_.checkpointRadius)) {
          if (isFinal) r.finishedAt = t;
          else w_.driveRacer(r.car, course_[++r.next], opt_.maxSpeed, opt_.drivingStyle);
        }
        if (r.finishedAt == -1 && state_ == State::Racing && t > 6) {
          if (w_.racerSpeed(r.car) > 2.f) { r.slowSince = -1; r.unstuck = 0; }
          else if (r.slowSince < 0) r.slowSince = now;
          else if (now - r.slowSince > 4.0) {  // stuck: drive on again, then put it back on the road
            if (r.unstuck++ % 2 == 1) {
              Vec3 ahead = course_[r.next];
              float len = dist2d(ahead, rp);
              Vec3 probe = len > 15 ? Vec3{rp.x + (ahead.x - rp.x) * 15 / len, rp.y + (ahead.y - rp.y) * 15 / len, rp.z} : ahead;
              Vec3 road;
              float hdg;
              if (w_.roadNode(probe, road, hdg)) w_.moveRacer(r.car, {road.x, road.y, road.z + 0.5f}, hdg);
            }
            w_.driveRacer(r.car, course_[r.next], opt_.maxSpeed, opt_.drivingStyle);
            r.slowSince = now;
          }
        }
        if (tune && state_ == State::Racing && opt_.catchUp > 0) {
          float behind = remaining(rp, r.next) - myLeft;  // > 0: further from the finish than you
          float p = 1.f + std::clamp((behind - 200.f) / 800.f, 0.f, 1.f) * opt_.catchUp;
          if (std::fabs(p - r.power) > 0.05f) { r.power = p; w_.setRacerPower(r.car, p); }
        }
      }
    }
    if (r.finishedAt == -1) aiDone = false;
    lastAi = std::max(lastAi, r.finishedAt);
  }

  // standings: finished by time, then by distance left along the course
  struct Entry { std::string name; double time; float left; bool me; };
  std::vector<Entry> e{{"You", playerFinish_, myLeft, true}};
  for (auto& r : racers_)
    e.push_back({r.model, r.finishedAt >= 0 ? r.finishedAt : -1,
                 r.finishedAt == -2 || !w_.racerOk(r.car) ? 1e9f : remaining(w_.racerPos(r.car), r.next), false});
  std::stable_sort(e.begin(), e.end(), [](const Entry& a, const Entry& b) {
    bool fa = a.time >= 0, fb = b.time >= 0;
    if (fa != fb) return fa;
    return fa ? a.time < b.time : a.left < b.left;
  });
  int pos = 1;
  for (size_t i = 0; i < e.size(); i++) if (e[i].me) pos = static_cast<int>(i) + 1;

  char line[128];
  if (state_ == State::Racing) {
    std::string cp = course_.size() > 1 ? "   checkpoint " + std::to_string(std::min(myNext_ + 1, course_.size())) + "/" +
                                              std::to_string(course_.size())
                                        : "";
    std::snprintf(line, sizeof line, "Position %d/%zu   %s   %.1f km to go%s", pos, e.size(),
                  raceTime(playerFinish_ >= 0 ? playerFinish_ : t).c_str(), myLeft / 1000.f, cp.c_str());
    w_.text(0.5f, 0.06f, line, 0.5f);
    bool over = (playerFinish_ >= 0 && (aiDone || t - playerFinish_ > kWaitAfterPlayer)) ||
                (aiDone && playerFinish_ < 0 && t - lastAi > kWaitAfterRacers) || t > kMaxRace;
    if (over) {
      state_ = State::Results;
      resultsUntil_ = now + kResultsSeconds;
      w_.target(course_.back(), false);
      char head[128];
      std::snprintf(head, sizeof head, "%s, %.1f km, %zu opponents:", courseName_.c_str(), raceKm_, racers_.size());
      std::string all = head;
      for (size_t i = 0; i < e.size(); i++)
        all += "  " + std::to_string(i + 1) + ". " + e[i].name + " " + raceTime(e[i].time);
      w_.saveResults(all);
    }
  }
  if (state_ == State::Results) {
    w_.text(0.5f, 0.28f, "RESULTS", 0.7f);
    for (size_t i = 0; i < e.size() && i < 8; i++) {
      std::snprintf(line, sizeof line, "%zu.  %-10s  %s", i + 1, e[i].name.c_str(), raceTime(e[i].time).c_str());
      w_.text(0.5f, 0.34f + 0.04f * static_cast<float>(i), line);
    }
    w_.text(0.5f, 0.34f + 0.04f * static_cast<float>(std::min<size_t>(e.size(), 8)) + 0.02f,
            "saved to gta_race_results.txt   (" + opt_.raceKey + " closes)", 0.35f);
  }
}

void Race::end(bool remove) {
  for (auto& r : racers_) w_.releaseRacer(r.car, remove);
  racers_.clear();
  if (!course_.empty()) w_.target(course_.back(), false);
  state_ = State::Idle;
}
