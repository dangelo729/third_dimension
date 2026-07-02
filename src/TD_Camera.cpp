// SPDX-License-Identifier: MIT

#include "TD_Camera.h"

#include <math.h>

namespace {

// fitMode_ intent selector values.
constexpr uint8_t kFitScene = 0;   // latch scene bounds at the next render
constexpr uint8_t kFitRadius = 1;  // fixed world radius around the target
constexpr uint8_t kFitFov = 2;     // vertical field-of-view intent
constexpr uint8_t kFitFocal = 3;   // raw projection scale (expert)

// orbit elevation clamp: +/-88.8 degrees in radians, keeping the pole
// basis non-degenerate.
constexpr float kOrbitElMax = 88.8f * 3.14159265358979323846f / 180.0f;

// world-to-view rotation for a pose at `eye` looking toward `target`:
//   f = normalize(target - eye)   (degenerate -> unit +Z)
//   r = normalize(cross(up, f))   (up parallel to f retried as (0, 0, 1))
//   u = cross(f, r)
// rows are { r, u, f }; u needs no renormalization because f and r are unit
// and orthogonal by construction.
TD_Mat3 lookRotation(TD_Vec3 eye, TD_Vec3 target, TD_Vec3 up) {
  TD_Vec3 f = tdNormalize(tdSub(target, eye));
  TD_Vec3 r = tdCross(up, f);
  if (tdDot(r, r) < 1e-12f) r = tdCross(tdVec3(0.0f, 0.0f, 1.0f), f);
  r = tdNormalize(r);
  TD_Vec3 u = tdCross(f, r);
  return {{r.x, r.y, r.z, u.x, u.y, u.z, f.x, f.y, f.z}};
}

}  // namespace

// documented defaults: auto-fit the scene (latched at first render) with
// margin 0.90, identity pose at the origin, nearSkip 0.1. the numeric
// defaults live in the header's member initializers; only the members
// without initializers are set here.
TD_Camera::TD_Camera() : rot_(tdMat3Identity()), fitMode_(kFitScene) {}

void TD_Camera::fitScene(float margin) {
  fitMode_ = kFitScene;
  fitMargin_ = margin;
  latched_ = false;  // (re)capture scene bounds at the next render
}

void TD_Camera::fitRadius(float worldRadius, float margin) {
  fitMode_ = kFitRadius;
  fitRadius_ = worldRadius;
  fitMargin_ = margin;
}

void TD_Camera::refit() { latched_ = false; }

void TD_Camera::setFov(float degrees) {
  fitMode_ = kFitFov;
  fovDeg_ = degrees;
}

void TD_Camera::setFocalPixels(float px) {
  fitMode_ = kFitFocal;
  focalPx_ = px;
}

// elevation clamps to +/-88.8 degrees (the pole basis degenerates at
// +/-90). recording orbit intent supersedes any explicit lookAt pose: the
// last pose intent wins.
void TD_Camera::setOrbit(float azimuth, float elevation) {
  if (elevation > kOrbitElMax) elevation = kOrbitElMax;
  if (elevation < -kOrbitElMax) elevation = -kOrbitElMax;
  orbitAz_ = azimuth;
  orbitEl_ = elevation;
  explicitPose_ = false;
}

void TD_Camera::setDistance(float worldUnits) { distance_ = worldUnits; }

void TD_Camera::setTarget(float x, float y, float z) {
  target_ = tdVec3(x, y, z);
  haveTarget_ = true;
}

// expert full pose; bypasses orbit resolution. rot_ rows become the
// { right, up, forward } basis of the requested look, eye_ = eye, and
// explicitPose_ latches so resolve() keeps this pose verbatim.
void TD_Camera::lookAt(TD_Vec3 eye, TD_Vec3 target, TD_Vec3 up) {
  rot_ = lookRotation(eye, target, up);
  eye_ = eye;
  explicitPose_ = true;
}

void TD_Camera::setCenterPixels(float cx, float cy) {
  cx_ = cx;
  cy_ = cy;
  haveCenterPx_ = true;
}

void TD_Camera::setNearSkip(float worldUnits) { nearSkip_ = worldUnits; }

// resolves framing intent to pose + focal for one frame:
//   - pose (unless lookAt() latched an explicit one):
//       eye = C + D * dir(orbitAz, orbitEl), with C = the target point (user
//       target or scene bounds center) and D = user distance or 4R; azimuth
//       0 places the eye in front of the scene on -Z, matching the light
//       direction convention
//   - auto-fit focal: focalForFit(R, D, margin * 0.5 * min(w, h))
//     (fitScene latches the scene bounds at the first render, refit()
//     re-latches; fitRadius uses the fixed user radius instead)
//   - FOV intent:     focal = focalForFovY(fovDeg_, targetH)
//   - raw focal:      focalPx_ used as-is
//   - projection center defaults to the target center unless
//     setCenterPixels() overrode it
// returns false when the intent cannot resolve (e.g. D <= R for a fit, or a
// nonpositive user focal).
bool TD_Camera::resolve(int16_t targetW, int16_t targetH, TD_Vec3 sceneCenter,
                        float sceneRadius) {
  if (!haveCenterPx_) {
    cx_ = (float)targetW * 0.5f;
    cy_ = (float)targetH * 0.5f;
  }
  if (fitMode_ == kFitScene && !latched_) {
    latchCenter_ = sceneCenter;
    latchRadius_ = sceneRadius;
    latched_ = true;
  }
  TD_Vec3 C = haveTarget_ ? target_ : (fitMode_ == kFitScene ? latchCenter_ : sceneCenter);
  float R = fitMode_ == kFitScene ? latchRadius_
                                  : (fitMode_ == kFitRadius ? fitRadius_ : sceneRadius);
  if (R <= 0.0f) R = 1.0f;  // empty scene still resolves to a sane frame
  float D;
  if (explicitPose_) {
    D = tdLength(tdSub(C, eye_));
  } else {
    D = distance_ > 0.0f ? distance_ : 4.0f * R;
    float ce = cosf(orbitEl_);
    TD_Vec3 dir = {-sinf(orbitAz_) * ce, sinf(orbitEl_), -cosf(orbitAz_) * ce};
    eye_ = tdAdd(C, tdScale(dir, D));
    rot_ = lookRotation(eye_, C, tdVec3(0.0f, 1.0f, 0.0f));
  }
  if (fitMode_ == kFitScene || fitMode_ == kFitRadius) {
    focalPx_ =
        focalForFit(R, D, fitMargin_ * 0.5f * (float)(targetW < targetH ? targetW : targetH));
  } else if (fitMode_ == kFitFov) {
    focalPx_ = focalForFovY(fovDeg_, targetH);
  }
  if (!(focalPx_ > 0.0f)) return false;
  return true;
}

// rows of the resolved world-to-view rotation. identity until resolve() or
// lookAt() populates rot_, matching the documented default pose.
TD_Vec3 TD_Camera::right() const { return tdVec3(rot_.m[0], rot_.m[1], rot_.m[2]); }
TD_Vec3 TD_Camera::up() const { return tdVec3(rot_.m[3], rot_.m[4], rot_.m[5]); }
TD_Vec3 TD_Camera::forward() const { return tdVec3(rot_.m[6], rot_.m[7], rot_.m[8]); }

// view = R_view * (p - eye), R_view rows = { right, up, forward }. the
// perspective divide happens in the renderer, never here.
TD_Vec3 TD_Camera::worldToView(TD_Vec3 p) const { return tdMat3MulVec(rot_, tdSub(p, eye_)); }

// standard pinhole relation: focal = (viewportH / 2) / tan(fovY / 2), the
// FOV converted to radians — half the viewport height subtends half the
// vertical field of view. out-of-range FOV (including NaN) or a nonpositive
// viewport returns 0.
float TD_Camera::focalForFovY(float fovYDeg, int16_t viewportH) {
  if (!(fovYDeg > 0.0f) || fovYDeg >= 180.0f || viewportH <= 0) return 0.0f;
  return ((float)viewportH * 0.5f) / tanf(fovYDeg * (3.14159265358979323846f / 180.0f) * 0.5f);
}

// focal = pixelRadius * sqrt(D*D - R*R) / R: a sphere of radius R at
// eye-distance D subtends a silhouette cone with
// tan(theta) = R / sqrt(D*D - R*R), so this focal projects it to exactly
// pixelRadius px of screen radius for any rotation — worst-case fit, the
// rim never clips. R = 1 reproduces the classic FIT_PX * sqrt(D*D - 1)
// expression bit-for-bit. requires D > R > 0; anything else (including
// NaN) returns 0.
float TD_Camera::focalForFit(float worldRadius, float distance, float pixelRadius) {
  if (!(distance > worldRadius && worldRadius > 0.0f)) return 0.0f;
  return pixelRadius * sqrtf(distance * distance - worldRadius * worldRadius) / worldRadius;
}
