#include "link_math.hpp"
#include <algorithm>
#include <cmath>

namespace {
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a) { float l = length(a); return l > 0 ? mul(a, 1.f / l) : a; }
Quat qmul(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quat qnormalize(Quat q) {
  float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 0 ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{0, 0, 0, 1};
}
}  // namespace

Vec3 acToGta(Vec3 a) { return {a.x, -a.z, a.y}; }
Vec3 gtaToAc(Vec3 g) { return {g.x, g.z, -g.y}; }

Quat acBasisToGtaQuat(Vec3 acForward, Vec3 acUp) {
  Vec3 f = normalize(acToGta(acForward));
  Vec3 u = normalize(acToGta(acUp));
  Vec3 r = normalize(cross(f, u));
  u = cross(r, f);  // re-orthogonalise
  // Rotation matrix columns: local X -> r, local Y -> f, local Z -> u.
  float m00 = r.x, m01 = f.x, m02 = u.x;
  float m10 = r.y, m11 = f.y, m12 = u.y;
  float m20 = r.z, m21 = f.z, m22 = u.z;
  float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0) {
    float s = std::sqrt(tr + 1.f) * 2;
    q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  } else if (m00 > m11 && m00 > m22) {
    float s = std::sqrt(1.f + m00 - m11 - m22) * 2;
    q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  } else if (m11 > m22) {
    float s = std::sqrt(1.f + m11 - m00 - m22) * 2;
    q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  } else {
    float s = std::sqrt(1.f + m22 - m00 - m11) * 2;
    q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
  }
  return qnormalize(q);
}

Pose predict(const Pose& p, float age) {
  age = std::clamp(age, 0.f, kMaxPredictSeconds);
  Pose out = p;
  out.pos = add(p.pos, mul(p.vel, age));
  float w = length(p.angVel);
  float angle = w * age;
  if (angle > 1e-6f) {
    Vec3 axis = mul(p.angVel, 1.f / w);
    float s = std::sin(angle / 2);
    Quat d{axis.x * s, axis.y * s, axis.z * s, std::cos(angle / 2)};
    out.rot = qnormalize(qmul(d, p.rot));  // world-space angular velocity: pre-multiply
  }
  return out;
}

std::optional<Impulse> hitImpulse(Vec3 vCmd, Vec3 vAfter, float dt, float massKg, Vec3 contactGta,
                                  float secondsSinceReset) {
  if (secondsSinceReset < kHitResetQuietSeconds) return std::nullopt;
  Vec3 dv = sub(vAfter, vCmd);
  dv.z += kGravity * dt;  // GTA's own gravity step is not a hit
  float m = length(dv);
  if (m < kHitMinDv) return std::nullopt;
  if (m > kHitMaxDv) dv = mul(dv, kHitMaxDv / m);
  return Impulse{gtaToAc(contactGta), gtaToAc(mul(dv, massKg))};
}

bool isOffSurface(float carZ, float groundZ) { return carZ < groundZ - kOffSurfaceMeters; }
