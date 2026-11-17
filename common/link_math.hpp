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

constexpr float kOffSurfaceMeters = 5.0f;
bool isOffSurface(float acCarHeightGta, float gtaGroundZ);

// World-space angular velocity (rad/s) that turns `from` into `to` over dt seconds (shortest way).
Vec3 angularVelocity(Quat from, Quat to, float dt);
// GTA heading in degrees (0 = north/+Y, counter-clockwise, 0..360) of a GTA-space forward vector.
float gtaHeadingDeg(Vec3 forwardGta);
// True when forward/up are usable: both roughly unit length and not parallel.
bool validBasis(Vec3 forward, Vec3 up);
