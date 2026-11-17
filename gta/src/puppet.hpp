// The GTA car that follows AC. Owns linking, unlinking, freezing and the on-screen text.
#pragma once
#include <string>
#include "game.hpp"
#include "link_client.hpp"

constexpr float kTeleportUnlinkMeters = 20.f;  // GTA moved the car this far by itself in one frame
constexpr double kMessageSeconds = 4.0;

class Puppet {
 public:
  // heightTrim: extra metres to lift the GTA car (gta_link.ini height_trim), on top of the automatic match.
  Puppet(Game& game, std::string model, float heightTrim = 0) : g_(game), model_(std::move(model)), trim_(heightTrim) {}
  // shmStatus: why the shared memory isn't open (shown when F8 can't link), empty if unknown.
  void tick(double nowSec, const volatile LinkState* raw, const std::string& shmStatus = "");
  bool linked() const { return veh_ != 0; }
  const std::string& message() const { return msg_; }

 private:
  void link(const LinkFrame& f, double now, const std::string& shmStatus);
  void unlink(const std::string& why, double now);
  bool calibrate(const LinkFrame& f, double now);  // sets lift_ from both cars' heights; false = try again
  void say(const std::string& m, double now) { msg_ = m; msgUntil_ = now + kMessageSeconds; }

  Game& g_;
  std::string model_;
  float trim_ = 0;
  float lift_ = 0;
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
