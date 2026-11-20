// Pure maths shared by the GTA .asi and the tests. No Windows or game headers here.
#pragma once
#include <optional>

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };

// AC is Y-up, GTA is Z-up, both metres: AC = (gta.x, gta.z, -gta.y).
Vec3 acToGta(Vec3 ac);
Vec3 gtaToAc(Vec3 gta);

// GTA quaternion whose local +Y is the car's forward and +Z its up, from AC world-space basis vectors.
Quat acBasisToGtaQuat(Vec3 acForward, Vec3 acUp);

struct Pose { Vec3 pos; Quat rot; Vec3 vel; Vec3 angVel; };  // GTA space, angVel in rad/s world
constexpr float kMaxPredictSeconds = 0.05f;
Pose predict(const Pose& p, float ageSeconds);

struct Impulse { Vec3 point; Vec3 impulse; };  // AC space, N*s
constexpr float kHitMinDv = 0.5f;            // m/s
constexpr float kHitMaxDv = 30.0f;           // m/s
constexpr float kHitResetQuietSeconds = 0.5f;
constexpr float kGravity = 9.81f;
std::optional<Impulse> hitImpulse(Vec3 vCommandedGta, Vec3 vAfterGta, float dt, float massKg,
                                  Vec3 contactPointGta, float secondsSinceReset);

// A GTA car or pedestrian hitting ours, worked out as a collision of two bodies. (GTA's own push on our car can't be
// used as it is: we press our car back into the other one every frame, and GTA pushes it out again every frame.)
// GTA's push gives the direction; the masses and how fast the two closed in give the size, once per contact.
// vBefore/vAfter: our velocity before/after GTA's physics step; vOther: the other's velocity after it;
// away: from the other's centre to ours (used when GTA's push is too small to tell a direction).
// restitution: 0 = the cars stay together, 1 = a perfect bounce. Returns our change of velocity, horizontal, GTA space.
constexpr float kHitRestitution = 0.25f;
Vec3 collisionDv(Vec3 vBefore, Vec3 vAfter, Vec3 vOther, float myMass, float otherMass, Vec3 away,
                 float restitution = kHitRestitution);

constexpr float kOffSurfaceMeters = 5.0f;
bool isOffSurface(float acCarHeightGta, float gtaGroundZ);

// World-space angular velocity (rad/s) that turns `from` into `to` over dt seconds (shortest way).
Vec3 angularVelocity(Quat from, Quat to, float dt);
// GTA heading in degrees (0 = north/+Y, counter-clockwise, 0..360) of a GTA-space forward vector.
float gtaHeadingDeg(Vec3 forwardGta);
// True when forward/up are usable: both roughly unit length and not parallel.
bool validBasis(Vec3 forward, Vec3 up);

// Rotate a vector by a unit quaternion.
Vec3 rotate(Quat q, Vec3 v);

// Velocity that makes a physically simulated car follow a moving target: target velocity plus a correction
// proportional to the position error. snap = too far behind, teleport instead.
struct Follow { bool snap; Vec3 vel; };
Follow followVelocity(Vec3 target, Vec3 current, Vec3 targetVel, float gainPerSec, float snapMeters);
// GTA steer bias (-1 full right .. +1 full left) as a player-input value for SET_CONTROL_VALUE_NEXT_FRAME
// on the steering axis (0 = full left, 0.5 = centre, 1 = full right).
float steerInputValue(float bias);
