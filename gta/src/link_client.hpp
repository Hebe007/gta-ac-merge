// Reads AC's LinkState from shared memory and turns it into a GTA-space pose for this frame.
#pragma once
#include <cstdint>
#include <string>
#include "link_math.hpp"
#include "link_protocol.h"
#include "seqlock.hpp"

struct LinkFrame {
  bool connected = false;  // shared memory present, magic/version right, sane data
  bool fresh = false;      // AC updated it within the last 0.25 s
  Pose pose{};             // GTA space, predicted to "now"
  float headingDeg = 0;    // GTA heading of the car
  float speedKmh = 0, rpm = 0, massKg = 0;
  int gear = 0;
  uint32_t resetCounter = 0;
  std::string problem;  // why connected is false although shared memory exists
};

class LinkClient {
 public:
  // `raw` may be null when the shared memory isn't open (AC not running).
  LinkFrame update(const volatile LinkState* raw, double nowSec);

 private:
  StalenessTracker stale_;
  bool have_ = false;
  LinkState last_{};
  double changedAt_ = 0;
  Quat rot_{0, 0, 0, 1};
  Vec3 angVel_{0, 0, 0};
};
