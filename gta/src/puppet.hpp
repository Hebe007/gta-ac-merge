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
  float steerLead = 0.06f;  // seconds to steer ahead of AC, hiding GTA's own steering smoothing (0 = off)
  bool autoReset = true;    // under the map for over a second: put the car back on its wheels right there
  float hitStrength = 1;    // how hard GTA traffic and pedestrians push your AC car (0 = not at all)
  float hitSideways = 1.5f;  // extra for the sideways part of a hit (AC's tyres soak up sideways shoves at once)
};

class Puppet {
 public:
  // heightTrim: extra metres to lift the GTA car (gta_link.ini height_trim), on top of the automatic match.
  Puppet(Game& game, std::string model, PuppetOptions opt = {}) : g_(game), model_(std::move(model)), opt_(opt) {}
  // shmStatus: why the shared memory isn't open (shown when F8 can't link), empty if unknown.
  // cmd: requests to the AC app (teleport), null if unavailable.
  // hits: where GTA traffic hits go to the AC app, null if unavailable.
  void tick(double nowSec, const volatile LinkState* raw, const std::string& shmStatus = "",
            volatile LinkCommand* cmd = nullptr, volatile HitBlock* hits = nullptr);
  int hitsSent() const { return hitsSent_; }
  bool linked() const { return veh_ != 0; }
  const std::string& message() const { return msg_; }

 private:
  void link(const LinkFrame& f, double now, const std::string& shmStatus);
  void unlink(const std::string& why, double now);
  bool calibrate(const LinkFrame& f, double now);
  bool offMap(Vec3 carPos);
  void requestTeleport(int vehicle, volatile LinkCommand* cmd, double now);
  void finishTeleport(const LinkFrame& f, double now);
  void adopt(const LinkFrame& f, double now, const std::string& why);
  void resetHere(double now, bool off);  // send the AC car upright onto GTA's ground at the current spot
  bool sendTeleport(Vec3 acPos, Vec3 acFwd);  // take over the car you're in  // car well below GTA's ground with nothing right under it  // sets lift_ from both cars' heights; false = try again
  void feedHit(const LinkFrame& f, double now);
  void feedTyres(const LinkFrame& f, double now);  // GTA burst a tyre: ask the AC app to blow it too
  void writeCommand();  // GTA car/pedestrian hit our car: tell the AC app
  void say(const std::string& m, double now) { msg_ = m; msgUntil_ = now + kMessageSeconds; }

  Game& g_;
  std::string model_;
  PuppetOptions opt_;
  float lift_ = 0;
  bool showDiag_ = false;
  bool wasOffMap_ = false;
  double offSince_ = -1, lastReset_ = -10, sunkSince_ = -1;
  float prevBias_ = 0;
  double prevBiasAt_ = -1;
  volatile LinkCommand* cmd_ = nullptr;
  volatile HitBlock* hits_ = nullptr;
  LinkCommand cmdOut_{};      // what we last wrote to the command block (teleport and tyre requests together)
  bool cmdStarted_ = false;
  uint32_t blowPending_ = 0;
  uint32_t knownBurst_ = 0;   // GTA tyres already handled (asked AC / kept burst to match AC)  // tyres asked to blow that AC doesn't report blown yet
  double blowSentAt_ = 0;
  double hitQuietUntil_ = 0;
  int hitsSent_ = 0;
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
