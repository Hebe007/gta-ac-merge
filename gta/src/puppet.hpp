// The GTA car that follows AC. Owns linking, unlinking, freezing and the on-screen text.
#pragma once
#include <string>
#include "game.hpp"
#include "link_client.hpp"

constexpr float kTeleportUnlinkMeters = 20.f;  // GTA moved the car this far by itself in one frame
constexpr double kMessageSeconds = 4.0;

class Puppet {
 public:
  Puppet(Game& game, std::string model) : g_(game), model_(std::move(model)) {}
  void tick(double nowSec, const volatile LinkState* raw);
  bool linked() const { return veh_ != 0; }
  const std::string& message() const { return msg_; }

 private:
  void link(const LinkFrame& f, double now);
  void unlink(const std::string& why, double now);
  void say(const std::string& m, double now) { msg_ = m; msgUntil_ = now + kMessageSeconds; }

  Game& g_;
  std::string model_;
  LinkClient client_;
  int veh_ = 0;
  bool haveLast_ = false;
  Pose last_{};
  std::string msg_;
  double msgUntil_ = 0;
};
