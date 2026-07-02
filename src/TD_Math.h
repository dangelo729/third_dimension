// SPDX-License-Identifier: MIT
#ifndef TD_MATH_H
#define TD_MATH_H

#include <math.h>
#include <stdint.h>

// minimal vector/matrix layer: POD structs + td-prefixed inline free
// functions, no operator overloading. the exact floating-point expression
// shapes below are API commitments — the host test suite pins bit-exact
// golden frames that depend on this evaluation order (and on building
// without -ffast-math / with -ffp-contract=off in the golden CI job).
// an algebraically equivalent "cleanup" is a behavioral change here.

struct TD_Vec3 {
  float x, y, z;
};

static inline TD_Vec3 tdVec3(float x, float y, float z) { return {x, y, z}; }
static inline TD_Vec3 tdAdd(TD_Vec3 a, TD_Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline TD_Vec3 tdSub(TD_Vec3 a, TD_Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline TD_Vec3 tdScale(TD_Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
static inline float tdDot(TD_Vec3 a, TD_Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline TD_Vec3 tdCross(TD_Vec3 a, TD_Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline float tdLength(TD_Vec3 v) { return sqrtf(tdDot(v, v)); }

// degenerate input returns `fallback` (unit +Z by default) instead of NaN.
static inline TD_Vec3 tdNormalize(TD_Vec3 v, TD_Vec3 fallback = {0.0f, 0.0f, 1.0f}) {
  float len2 = tdDot(v, v);
  if (len2 < 1e-12f) return fallback;
  float inv = 1.0f / sqrtf(len2);
  return {v.x * inv, v.y * inv, v.z * inv};
}

// row-major 3x3, column-vector convention: v' = M*v, element m[3*row + col].
// composition applies right-to-left: (A*B)*v = A*(B*v).
struct TD_Mat3 {
  float m[9];
};

static inline TD_Mat3 tdMat3Identity() {
  return {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
}

// positive angles are counter-clockwise about the axis viewed from the
// positive axis end (right-hand rule).
static inline TD_Mat3 tdMat3RotX(float a) {
  float c = cosf(a), s = sinf(a);
  return {{1, 0, 0, 0, c, -s, 0, s, c}};
}
static inline TD_Mat3 tdMat3RotY(float a) {
  float c = cosf(a), s = sinf(a);
  return {{c, 0, s, 0, 1, 0, -s, 0, c}};
}
static inline TD_Mat3 tdMat3RotZ(float a) {
  float c = cosf(a), s = sinf(a);
  return {{c, -s, 0, s, c, 0, 0, 0, 1}};
}

static inline TD_Mat3 tdMat3Mul(const TD_Mat3 &a, const TD_Mat3 &b) {
  TD_Mat3 o;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      o.m[r * 3 + c] =
          a.m[r * 3] * b.m[c] + a.m[r * 3 + 1] * b.m[3 + c] + a.m[r * 3 + 2] * b.m[6 + c];
  return o;
}

static inline TD_Vec3 tdMat3MulVec(const TD_Mat3 &M, TD_Vec3 v) {
  return {M.m[0] * v.x + M.m[1] * v.y + M.m[2] * v.z,
          M.m[3] * v.x + M.m[4] * v.y + M.m[5] * v.z,
          M.m[6] * v.x + M.m[7] * v.y + M.m[8] * v.z};
}

static inline TD_Mat3 tdMat3Transpose(const TD_Mat3 &M) {
  return {{M.m[0], M.m[3], M.m[6], M.m[1], M.m[4], M.m[7], M.m[2], M.m[5], M.m[8]}};
}

static inline TD_Mat3 tdMat3ScaleUniform(const TD_Mat3 &M, float s) {
  TD_Mat3 o;
  for (int i = 0; i < 9; ++i) o.m[i] = M.m[i] * s;
  return o;
}

// R = Rx*(Ry*Rz): Z (roll) applies first, then Y (yaw), then X (pitch).
// composed with two tdMat3Mul calls in exactly this order — the object
// rotation convention the whole library (and its golden tests) commits to.
static inline TD_Mat3 tdEulerXYZ(float rx, float ry, float rz) {
  return tdMat3Mul(tdMat3RotX(rx), tdMat3Mul(tdMat3RotY(ry), tdMat3RotZ(rz)));
}

// affine transform: rotation*uniform-scale linear part + translation.
// normals never go through TD_Xform — they use the unscaled rotation only
// (uniform scale leaves normal directions unchanged, so no renormalization
// is ever needed in the shading loop).
struct TD_Xform {
  TD_Mat3 m;
  TD_Vec3 t;
};

// apply b first, then a: result(v) = a(b(v)).
static inline TD_Xform tdXformCompose(const TD_Xform &a, const TD_Xform &b) {
  return {tdMat3Mul(a.m, b.m), tdAdd(tdMat3MulVec(a.m, b.t), a.t)};
}

// translation is added last, matching the renderer's per-vertex expression.
static inline TD_Vec3 tdXformPoint(const TD_Xform &x, TD_Vec3 p) {
  TD_Vec3 r = tdMat3MulVec(x.m, p);
  return {r.x + x.t.x, r.y + x.t.y, r.z + x.t.z};
}

// transforms a direction/displacement; result length = input length * scale
// (not guaranteed unit).
static inline TD_Vec3 tdXformDir(const TD_Xform &x, TD_Vec3 d) { return tdMat3MulVec(x.m, d); }

// position + euler XYZ + uniform scale -> { s*(Rx*Ry*Rz), position }
static inline TD_Xform tdXformFromPRS(TD_Vec3 pos, float rx, float ry, float rz, float s) {
  return {tdMat3ScaleUniform(tdEulerXYZ(rx, ry, rz), s), pos};
}

#endif  // TD_MATH_H
