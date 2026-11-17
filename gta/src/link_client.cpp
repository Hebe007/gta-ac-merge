#include "link_client.hpp"
#include <cstdio>

LinkFrame LinkClient::update(const volatile LinkState* raw, double now) {
  LinkFrame f;
  if (!raw) return f;
  LinkState s;
  if (!seqRead(raw, s)) {
    if (!have_) { f.problem = "writer busy (seq " + std::to_string(s.seq) + ")"; return f; }
    s = last_;  // writer busy this instant: reuse the last good state
  }
  if (s.magic != LINK_MAGIC) { f.problem = "wrong magic " + std::to_string(s.magic); return f; }
  if (s.version != LINK_VERSION) { f.problem = "version " + std::to_string(s.version) + ", expected " + std::to_string(LINK_VERSION); return f; }
  Vec3 fwd{s.fwd[0], s.fwd[1], s.fwd[2]}, up{s.up[0], s.up[1], s.up[2]};
  if (!validBasis(fwd, up)) {
    char b[128];
    std::snprintf(b, sizeof b, "bad direction fwd %.2f %.2f %.2f up %.2f %.2f %.2f", fwd.x, fwd.y, fwd.z, up.x, up.y, up.z);
    f.problem = b;
    return f;
  }

  stale_.onRead(s.seq, now);
  if (!have_ || s.seq != last_.seq) {
    Quat r = acBasisToGtaQuat(fwd, up);
    // Angular velocity from successive orientations: needs no assumption about AC's axis handedness.
    bool sameRun = have_ && s.resetCounter == last_.resetCounter;
    angVel_ = sameRun ? angularVelocity(rot_, r, static_cast<float>(now - changedAt_)) : Vec3{0, 0, 0};
    rot_ = r;
    changedAt_ = now;
    last_ = s;
    have_ = true;
  }

  f.connected = true;
  f.fresh = !stale_.stale(now);
  Pose p{acToGta({last_.pos[0], last_.pos[1], last_.pos[2]}), rot_,
         acToGta({last_.vel[0], last_.vel[1], last_.vel[2]}), angVel_};
  f.pose = f.fresh ? predict(p, static_cast<float>(now - changedAt_)) : p;
  f.headingDeg = gtaHeadingDeg(acToGta({last_.fwd[0], last_.fwd[1], last_.fwd[2]}));
  f.speedKmh = last_.speedKmh;
  f.rpm = last_.rpm;
  f.massKg = last_.massKg;
  f.gear = last_.gear;
  f.resetCounter = last_.resetCounter;
  return f;
}
