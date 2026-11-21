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

void Puppet::tick(double now, const volatile LinkState* raw, const std::string& shmStatus,
                  volatile LinkCommand* cmd, volatile HitBlock* hits) {
  hits_ = hits;
  LinkFrame f = client_.update(raw, now);
  cmd_ = cmd;

  if (g_.linkKeyPressed()) {
    if (linked()) unlink("GTA link off", now);
    else if (pendingVeh_) { pendingVeh_ = 0; say("GTA link: cancelled", now); }
    else if (cmd && f.connected && f.fresh && g_.playerVehicle()) requestTeleport(g_.playerVehicle(), cmd, now);
    else link(f, now, raw ? "data rejected: " + f.problem : shmStatus);
  }
  if (pendingVeh_) finishTeleport(f, now);
  if (g_.diagKeyPressed()) showDiag_ = !showDiag_;

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
        feedHit(f, now);  // before setPose: GTA's physics since last frame
        feedTyres(f, now);
        g_.setPose(veh_, last_);
        float lock = f.steerLockDeg > 1.f ? f.steerLockDeg : 450.f;  // AC: + = right; GTA: + = left
        float bias = -f.steerDeg / lock * opt_.steerScale * (opt_.steerInvert ? -1.f : 1.f) + opt_.steerOffset;
        float ahead = bias;
        if (prevBiasAt_ >= 0 && now > prevBiasAt_ && opt_.steerLead > 0) {
          float rate = (bias - prevBias_) / static_cast<float>(now - prevBiasAt_);
          ahead = bias + rate * opt_.steerLead;
        }
        prevBias_ = bias;
        prevBiasAt_ = now;
        g_.setSteer(veh_, std::fmax(-1.f, std::fmin(1.f, ahead)));
        if (opt_.pedals) g_.setPedals(veh_, f.gas, f.brake);
        bool off = offMap(last_.pos);
        if (off) g_.drawText(0.5f, 0.12f, "Off the map - reset the car in Assetto Corsa (spot saved to gta_link_holes.txt)");
        if (off && !wasOffMap_) g_.logHole(last_.pos);
        wasOffMap_ = off;
        if (!off) offSince_ = -1;
        else if (offSince_ < 0) offSince_ = now;
        else if (opt_.autoReset && now - offSince_ > 1.0 && now - lastReset_ > 2.0) resetHere(now, true);
        // Sunk into the road (after a big landing or a crash) and not moving: GTA's ground is above the car's centre.
        bool sunk = false;
        float sz;
        if (!off && f.speedKmh < 10.f && g_.groundZ({last_.pos.x, last_.pos.y, last_.pos.z + 1.f}, sz))
          sunk = sz > last_.pos.z + 0.15f && sz <= last_.pos.z + 1.f;
        if (!sunk) sunkSince_ = -1;
        else if (sunkSince_ < 0) sunkSince_ = now;
        else if (opt_.autoReset && now - sunkSince_ > 1.0 && now - lastReset_ > 2.0) {
          resetHere(now, false);
          say("Stuck in the ground - put back on the road here", now);
          sunkSince_ = -1;
        }
        if (g_.resetKeyPressed()) {
          // what the stuck car looked like: where GTA's ground is around the car's centre, and how fast AC says it goes
          auto probe = [&](float above) {
            float z;
            char b[16];
            if (!g_.groundZ({last_.pos.x, last_.pos.y, last_.pos.z + above}, z)) return std::string("none");
            std::snprintf(b, sizeof b, "%+.2f", z - last_.pos.z);
            return std::string(b);
          };
          char line[200];
          std::snprintf(line, sizeof line, "%.1f,%.1f,%.1f  speed %.0f km/h  gas %.2f  ground from +1m: %s  +2m: %s  +4m: %s",
                        last_.pos.x, last_.pos.y, last_.pos.z, f.speedKmh, f.gas, probe(1.f).c_str(),
                        probe(2.f).c_str(), probe(4.f).c_str());
          g_.logStuck(line);
          resetHere(now, off);
        }
        if (showDiag_) {
          char d[160];
          std::snprintf(d, sizeof d, "AC gas %.2f brake %.2f steer %.0f   pedals %d   hits sent %d%s", f.gas,
                        f.brake, f.steerDeg, opt_.pedals ? 1 : 0, hitsSent_, hits_ ? "" : " (no hits memory)");
          g_.drawText(0.5f, 0.80f, d);
          g_.drawText(0.5f, 0.84f, g_.diagnostics(veh_));
        }
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

void Puppet::feedHit(const LinkFrame& f, double now) {
  Game::Hit h;
  if (!g_.takeHit(veh_, h) || !hits_ || opt_.hitStrength <= 0) return;  // (always asked: it also tells GTA's side
                                                                         // not to hold the car frozen for this contact)
  // Once per contact: the AC car's new speed takes a couple of frames to reach GTA; until then GTA still sees
  // the old speed pressing into the other car, which is not a new hit.
  if (now < hitQuietUntil_) return;
  float mass = f.massKg > 100.f ? f.massKg : 1300.f;
  Vec3 dv = collisionDv(h.vCommanded, h.vAfter, h.vOther, mass, h.otherMass, h.away);
  float size = std::sqrt(dv.x * dv.x + dv.y * dv.y);
  if (size < kHitMinDv) return;
  if (size > kHitMaxDv) dv = {dv.x * kHitMaxDv / size, dv.y * kHitMaxDv / size, 0};
  // AC's tyres grip at once and soak up a sideways shove: give the sideways part of the hit extra
  Vec3 fw = rotate(last_.rot, {0, 1, 0});
  float fl = std::sqrt(fw.x * fw.x + fw.y * fw.y);
  if (fl > 1e-3f && opt_.hitSideways != 1.f) {
    fw = {fw.x / fl, fw.y / fl, 0};
    float along = dv.x * fw.x + dv.y * fw.y;
    Vec3 side = {dv.x - along * fw.x, dv.y - along * fw.y, 0};
    dv = {along * fw.x + side.x * opt_.hitSideways, along * fw.y + side.y * opt_.hitSideways, 0};
  }
  hitQuietUntil_ = now + 0.2;
  Impulse imp0{gtaToAc(h.point), gtaToAc({dv.x * mass, dv.y * mass, dv.z * mass})};
  const Impulse* imp = &imp0;
  HitBlock b;
  for (int i = 0; i < 4 && !seqRead(hits_, b); i++) {}
  uint32_t slot = (b.count % kHitRing) * 3;
  b.point[slot] = imp->point.x; b.point[slot + 1] = imp->point.y; b.point[slot + 2] = imp->point.z;
  b.impulse[slot] = imp->impulse.x * opt_.hitStrength;
  b.impulse[slot + 1] = imp->impulse.y * opt_.hitStrength;
  b.impulse[slot + 2] = imp->impulse.z * opt_.hitStrength;
  b.count++;
  seqWrite(hits_, b);
  hitsSent_++;
}

// Tyres: GTA bursts one (spike strip, a cop shooting it) -> ask AC to blow it; while AC has it blown, GTA's stays
// burst too; once AC no longer has it blown (repaired, F9 repair), GTA's is fixed.
void Puppet::feedTyres(const LinkFrame& f, double now) {
  uint32_t gta = g_.burstTyres(veh_);
  if (blowPending_ && (f.tyresBlown & blowPending_) == blowPending_) blowPending_ = 0;  // AC did it
  if (blowPending_ && now - blowSentAt_ > 2.0) blowPending_ = 0;                        // AC didn't: give up
  uint32_t fresh = gta & ~knownBurst_ & ~f.tyresBlown;  // burst since we last looked
  if (fresh && cmd_ && now >= hitQuietUntil_) {
    if (!cmdStarted_) {
      cmdOut_.blowId = cmd_->blowId;
      cmdOut_.teleportId = teleportId_ = cmd_->teleportId;  // keep the teleport id as it is: no new teleport
      cmdStarted_ = true;
    }
    blowPending_ |= fresh;
    knownBurst_ |= fresh;
    blowSentAt_ = now;
    cmdOut_.blowId++;
    cmdOut_.blowMask = blowPending_;
    writeCommand();
    say(std::string("Tyre burst: ") + ((fresh & 3) ? "front" : "rear"), now);
  }
  uint32_t keep = f.tyresBlown | blowPending_;
  g_.syncTyres(veh_, keep);
  knownBurst_ = (knownBurst_ | (gta & f.tyresBlown)) & keep;  // the rest GTA has just fixed
}

void Puppet::writeCommand() {
  cmdOut_.magic = LINK_MAGIC;
  cmdOut_.version = LINK_VERSION;
  cmdOut_.seq = cmd_->seq;
  seqWrite(cmd_, cmdOut_);
}

bool Puppet::sendTeleport(Vec3 acPos, Vec3 acFwd) {
  if (!cmd_) return false;
  if (!cmdStarted_) {  // continue the ids already in the block (the AC app ignores ids it has seen)
    cmdOut_.blowId = cmd_->blowId;
    cmdStarted_ = true;
  }
  cmdOut_.teleportId = ++teleportId_;
  cmdOut_.pos[0] = acPos.x; cmdOut_.pos[1] = acPos.y; cmdOut_.pos[2] = acPos.z;
  cmdOut_.fwd[0] = acFwd.x; cmdOut_.fwd[1] = acFwd.y; cmdOut_.fwd[2] = acFwd.z;
  writeCommand();
  return true;
}

void Puppet::resetHere(double now, bool off) {
  Vec3 p = last_.pos;
  float gz = p.z;
  bool found = off ? g_.groundZ({p.x, p.y, p.z + 50.f}, gz)  // fell through: the ground is above us
                   : g_.groundZ({p.x, p.y, p.z + 1.5f}, gz) && gz <= p.z + 1.5f && gz > p.z - 3.f;  // also sunk in
  if (!found) gz = p.z;
  Vec3 fwd = rotate(last_.rot, {0, 1, 0});
  fwd.z = 0;  // level, wheels down
  float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
  fwd = len > 1e-3f ? Vec3{fwd.x / len, fwd.y / len, 0} : Vec3{0, 1, 0};
  if (sendTeleport(gtaToAc({p.x, p.y, gz + 1.f}), gtaToAc(fwd))) {
    lastReset_ = now;
    hitQuietUntil_ = now + 1.0;
    say(off ? "Fell through - put back on the road here" : "Car reset here (F9)", now);
  }
}

void Puppet::requestTeleport(int veh, volatile LinkCommand* cmd, double now) {
  Vec3 p = g_.position(veh);
  float gz;
  float z = g_.groundZ({p.x, p.y, p.z + 1.f}, gz) && gz <= p.z + 1.f && gz > p.z - 3.f ? gz : p.z - 0.5f;
  Vec3 acPos = gtaToAc({p.x, p.y, z + 1.f});  // drop it from 1 m above GTA's ground
  Vec3 f = g_.forward(veh);
  float len = std::sqrt(f.x * f.x + f.y * f.y);
  Vec3 level = len > 1e-3f ? Vec3{f.x / len, f.y / len, 0} : Vec3{0, 1, 0};  // wheels down
  Vec3 acFwd = gtaToAc(level);
  cmd_ = cmd;
  sendTeleport(acPos, acFwd);
  pendingVeh_ = veh;
  pendingSince_ = now;
  say("GTA link: moving the Assetto Corsa car to this car...", now);
}

void Puppet::finishTeleport(const LinkFrame& f, double now) {
  bool answered = f.connected && f.teleportAck == teleportId_;
  if (answered && f.teleportStatus < 0) {
    adopt(f, now, "Assetto Corsa can't move the car (CSP refused) - your car was brought to the AC car instead");
    return;
  }
  Vec3 target = g_.position(pendingVeh_);
  float dx = f.pose.pos.x - target.x, dy = f.pose.pos.y - target.y, dz = f.pose.pos.z - target.z;
  bool arrived = answered && f.teleportStatus > 0 && f.fresh && dx * dx + dy * dy + dz * dz < 3.f * 3.f;
  if (arrived) {
    adopt(f, now, "");
  } else if (now - pendingSince_ > 3.0) {
    adopt(f, now, answered ? "Assetto Corsa said it moved the car but it didn't move (CSP ignores teleports from apps?)"
                             " - your car was brought to the AC car instead"
                           : "Assetto Corsa didn't answer (is the GTA Link app up to date?) - your car was brought to"
                             " the AC car instead");
  }
}

void Puppet::adopt(const LinkFrame& f, double now, const std::string& why) {
  veh_ = pendingVeh_;
  pendingVeh_ = 0;
  adopted_ = true;
  g_.prepare(veh_);
  gtaH_ = g_.originHeight(veh_);
  last_ = f.pose;
  haveLast_ = false;  // the car is about to jump to AC's position; don't mistake that for a trainer teleport
  lift_ = opt_.heightTrim;
  hitQuietUntil_ = now + 1.0;  // the car jumping to AC's position is not a hit
  calPending_ = !calibrate(f, now);
  calUntil_ = now + 5.0;
  if (calPending_) say("GTA link on (F8 to turn off) - measuring height...", now);
  if (!why.empty()) say(why + ". " + msg_, now);
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
  hitQuietUntil_ = now + 1.0;  // the car jumping to AC's position is not a hit
  calPending_ = !calibrate(f, now);
  calUntil_ = now + 5.0;
  if (calPending_) say("GTA link on (F8 to turn off) - measuring height...", now);
}

bool Puppet::offMap(Vec3 p) {
  // Ground right under the car (bridges, overpasses, trees above don't matter)? Then it's fine.
  float gz;
  if (g_.groundZ({p.x, p.y, p.z + 1.f}, gz) && gz <= p.z + 1.f && gz > p.z - 3.f) return false;
  // Otherwise: is GTA's ground well above us, i.e. did we fall through?
  return g_.groundZ({p.x, p.y, p.z + 50.f}, gz) && isOffSurface(p.z, gz);
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
  char m[256];
  std::snprintf(m, sizeof m, "GTA link on (F8 to turn off) - height AC %.2f m, GTA %.2f m, lift %+.2f m  [link v%u, built %s %s]",
                acH, gtaH_, lift_, LINK_VERSION, __DATE__, __TIME__);
  say(m, now);
  return true;
}

void Puppet::unlink(const std::string& why, double now) {
  if (veh_ && g_.exists(veh_)) {
    if (adopted_) g_.release(veh_);  // your own car stays
    else g_.deleteVehicle(veh_);
  }
  adopted_ = false;
  veh_ = 0;
  haveLast_ = false;
  say(why, now);
}
