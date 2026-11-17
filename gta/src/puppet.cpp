#include "puppet.hpp"
#include <cmath>
#include <cstdio>

namespace {
float dist(Vec3 a, Vec3 b) {
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
// Protocol gear uses CSP numbering: -1 = reverse, 0 = neutral, 1 = first...
std::string gearName(int g) { return g < 0 ? "R" : g == 0 ? "N" : std::to_string(g); }
}  // namespace

void Puppet::tick(double now, const volatile LinkState* raw, const std::string& shmStatus) {
  LinkFrame f = client_.update(raw, now);

  if (g_.linkKeyPressed()) {
    if (linked()) unlink("GTA link off", now);
    else link(f, now, raw ? "data rejected: " + f.problem : shmStatus);
  }

  if (linked()) {
    if (!g_.exists(veh_) || g_.playerDeadOrArrested() || g_.playerVehicle() != veh_) {
      unlink("GTA link off: you left the car", now);
    } else {
      g_.disableVehicleControls();
      Vec3 cur = g_.position(veh_);
      if (haveLast_ && dist(cur, last_.pos) > kTeleportUnlinkMeters) {
        unlink("GTA link off: the car was teleported (press F8 to link again)", now);
      } else if (f.connected && f.fresh) {
        if (calPending_) calPending_ = now < calUntil_ && !calibrate(f, now);
        last_ = f.pose;
        Vec3 up = rotate(last_.rot, {0, 0, 1});
        last_.pos = {last_.pos.x + up.x * lift_, last_.pos.y + up.y * lift_, last_.pos.z + up.z * lift_};
        haveLast_ = true;
        g_.setPose(veh_, last_);
        float lock = f.steerLockDeg > 1.f ? f.steerLockDeg : 450.f;  // AC: + = right; GTA: + = left
        float bias = -f.steerDeg / lock * opt_.steerScale * (opt_.steerInvert ? -1.f : 1.f) + opt_.steerOffset;
        g_.setSteer(veh_, std::fmax(-1.f, std::fmin(1.f, bias)));
        float gz;
        if (g_.groundZ({last_.pos.x, last_.pos.y, last_.pos.z + 50.f}, gz) && isOffSurface(last_.pos.z, gz))
          g_.drawText(0.5f, 0.12f, "Off the map - reset the car in Assetto Corsa");
        char hud[96];
        std::snprintf(hud, sizeof hud, "%.0f km/h   gear %s   %.0f rpm", f.speedKmh, gearName(f.gear).c_str(), f.rpm);
        g_.drawText(0.85f, 0.92f, hud);
      } else {
        Pose frozen = last_;
        frozen.vel = {0, 0, 0};
        frozen.angVel = {0, 0, 0};
        g_.setPose(veh_, frozen);
        g_.drawText(0.5f, 0.12f, "GTA link: waiting for Assetto Corsa");
      }
    }
  }

  if (now < msgUntil_) g_.drawText(0.5f, 0.05f, msg_);
}

void Puppet::link(const LinkFrame& f, double now, const std::string& shmStatus) {
  if (!f.connected) {
    say("GTA link: can't see Assetto Corsa's GTA Link data - is the app running?" +
            (shmStatus.empty() ? std::string() : " (" + shmStatus + ")"), now);
    return;
  }
  if (!f.fresh) {
    say("GTA link: Assetto Corsa's data is not updating - is AC paused or asleep in the background?", now);
    return;
  }
  veh_ = g_.spawnVehicle(model_, f.pose.pos, f.headingDeg);
  if (!veh_) {
    say("GTA link: could not spawn " + model_, now);
    return;
  }
  g_.putPlayerIn(veh_);
  gtaH_ = g_.originHeight(veh_);
  last_ = f.pose;
  haveLast_ = true;
  lift_ = opt_.heightTrim;
  calPending_ = !calibrate(f, now);
  calUntil_ = now + 5.0;
  if (calPending_) say("GTA link on (F8 to turn off) - measuring height...", now);
}

bool Puppet::calibrate(const LinkFrame& f, double now) {
  float acH = f.rideHeight;
  if (acH <= 0.05f) {
    // AC didn't say: the car is standing on the road, so measure AC's position against GTA's ground.
    float gz;
    Vec3 p = f.pose.pos;
    if (!g_.groundZ({p.x, p.y, p.z + 1.5f}, gz) || p.z - gz <= -0.2f || p.z - gz >= 1.5f) return false;
    acH = p.z - gz;
  }
  lift_ = (gtaH_ > 0 ? gtaH_ - acH : 0.f) + opt_.heightTrim;
  char m[160];
  std::snprintf(m, sizeof m, "GTA link on (F8 to turn off) - height AC %.2f m, GTA %.2f m, lift %+.2f m", acH, gtaH_, lift_);
  say(m, now);
  return true;
}

void Puppet::unlink(const std::string& why, double now) {
  if (veh_ && g_.exists(veh_)) g_.deleteVehicle(veh_);
  veh_ = 0;
  haveLast_ = false;
  say(why, now);
}
