// The GTA car that follows AC. Owns linking, unlinking, freezing and the on-screen text.
#pragma once
#include <string>
#include "game.hpp"
#include "link_client.hpp"

constexpr float kTeleportUnlinkMeters = 20.f;  // GTA moved the car this far by itself in one frame
constexpr double kMessageSeconds = 4.0;

// Tuning from gta_link.ini.
struct PuppetOptions {
  float heightTrim = 0;     // extra metres to lift the GTA car, on top of the automatic match
  bool steerInvert = false;  // flip steering if GTA's wheels turn the wrong way
  float steerScale = 1;     // GTA wheel angle per AC wheel angle (lower if GTA hits full lock too early)
  float steerOffset = 0;    // added to the centre (-1..1, + = left) if GTA's wheels aren't straight
  bool pedals = true;       // pass AC's throttle/brake to GTA (rev counter, brake lights)
};

class Puppet {
 public:
  // heightTrim: extra metres to lift the GTA car (gta_link.ini height_trim), on top of the automatic match.
  Puppet(Game& game, std::string model, PuppetOptions opt = {}) : g_(game), model_(std::move(model)), opt_(opt) {}
  // shmStatus: why the shared memory isn't open (shown when F8 can't link), empty if unknown.
  // cmd: requests to the AC app (teleport), null if unavailable.
  void tick(double nowSec, const volatile LinkState* raw, const std::string& shmStatus = "",
            volatile LinkCommand* cmd = nullptr);
  bool linked() const { return veh_ != 0; }
  const std::string& message() const { return msg_; }

 private:
  void link(const LinkFrame& f, double now, const std::string& shmStatus);
  void unlink(const std::string& why, double now);
  bool calibrate(const LinkFrame& f, double now);
  bool offMap(Vec3 carPos);
  void requestTeleport(int vehicle, volatile LinkCommand* cmd, double now);
  void finishTeleport(const LinkFrame& f, double now);
  void fallbackSpawn(const LinkFrame& f, double now, const std::string& why);  // car well below GTA's ground with nothing right under it  // sets lift_ from both cars' heights; false = try again
  void say(const std::string& m, double now) { msg_ = m; msgUntil_ = now + kMessageSeconds; }

  Game& g_;
  std::string model_;
  PuppetOptions opt_;
  float lift_ = 0;
  bool showDiag_ = false;
  bool adopted_ = false;       // the car you were in, not one we spawned
  int pendingVeh_ = 0;         // waiting for the AC app to move the AC car here
  uint32_t teleportId_ = 0;
  double pendingSince_ = 0;
  float gtaH_ = 0;
  bool calPending_ = false;
  double calUntil_ = 0;
  LinkClient client_;
  int veh_ = 0;
  bool haveLast_ = false;
  Pose last_{};
  std::string msg_;
  double msgUntil_ = 0;
};
