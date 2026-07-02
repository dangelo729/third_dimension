// SPDX-License-Identifier: MIT
#ifndef TD_CAMERA_H
#define TD_CAMERA_H

#include <stdint.h>

#include "TD_Math.h"

// stores framing intent and resolves it against the render target's
// dimensions and the scene's bounds at render time. the resolved pose is an
// orthonormal basis + eye point (rows: right, up, forward; view +Z points
// into the scene); pose setters build the basis, so there is no gimbal
// state to accumulate.
//
// defaults give a working first frame with zero camera code: auto-fit the
// scene bounds (latched at the first render), orbit (0, 0), distance 4x the
// bound radius, look-at the bounds center, projection center = target
// center.
class TD_Camera {
 public:
  TD_Camera();

  // ---- framing intent (pick one; default = fitScene latched at first render) ----
  void fitScene(float margin = 0.90f);   // latch scene bounds at next render
  // for animated scenes whose objects move: frame a fixed world radius
  // around the target point, not the latched first-frame bounds.
  void fitRadius(float worldRadius, float margin = 0.90f);
  void refit();                          // re-latch auto-fit bounds next frame
  void setFov(float degrees);            // vertical field of view intent
  void setFocalPixels(float px);         // expert: raw projection scale

  // ---- pose ----
  void setOrbit(float azimuth, float elevation);  // radians, around the target point
  // default: 4 x the fit radius. fit intents fail resolve() with
  // TD_ERR_BAD_CAMERA unless the distance exceeds the fit radius.
  void setDistance(float worldUnits);
  void setTarget(float x, float y, float z);      // look-at point; default bounds center
  // expert full pose: forward = normalize(target - eye), right/up built from
  // `up` (degenerate up falls back to +Z). overrides orbit/distance intent.
  void lookAt(TD_Vec3 eye, TD_Vec3 target, TD_Vec3 up = {0.0f, 1.0f, 0.0f});

  // ---- projection details ----
  void setCenterPixels(float cx, float cy);  // principal point override
  // faces with any loop vertex closer than this (view z) are skipped whole —
  // there is no near-plane clipping; this bounds focal/z blowup.
  void setNearSkip(float worldUnits);

  // ---- resolved state (used by the renderer; public for host tests) ----
  // resolves intent -> pose + focal for a frame. sceneCenter/sceneRadius
  // describe the world bound sphere of the visible objects (the renderer
  // computes them; radius <= 0 means an empty scene). returns false if the
  // intent cannot resolve (e.g. distance <= radius for a fit).
  bool resolve(int16_t targetW, int16_t targetH, TD_Vec3 sceneCenter, float sceneRadius);

  TD_Vec3 right() const;    // basis row 0
  TD_Vec3 up() const;       // basis row 1
  TD_Vec3 forward() const;  // basis row 2 (into the scene)
  TD_Vec3 eye() const { return eye_; }
  float focalPixels() const { return focalPx_; }
  float nearSkip() const { return nearSkip_; }
  float centerX() const { return cx_; }
  float centerY() const { return cy_; }
  TD_Vec3 worldToView(TD_Vec3 p) const;  // rot * (p - eye)

  // standard pinhole relation: half the viewport height subtends half the FOV.
  static float focalForFovY(float fovYDeg, int16_t viewportH);
  // worst-case fit: a sphere of radius R at eye-distance D is bounded by its
  // silhouette cone (tan(theta) = R / sqrt(D*D - R*R)); this focal makes it
  // project to exactly pixelRadius px for any rotation. requires D > R > 0;
  // returns 0 on violation. R = 1 reproduces the classic
  // FIT_PX * sqrt(D*D - 1) expression bit-for-bit.
  static float focalForFit(float worldRadius, float distance, float pixelRadius);

 private:
  // resolved pose
  TD_Mat3 rot_;         // world-to-view rotation; rows = right, up, forward
  TD_Vec3 eye_ = {0.0f, 0.0f, 0.0f};
  float focalPx_ = 100.0f;
  float cx_ = 0.0f, cy_ = 0.0f;
  float nearSkip_ = 0.1f;
  // intent
  uint8_t fitMode_;     // scene-latch / fixed-radius / fov / raw-focal
  float fitMargin_ = 0.90f;
  float fitRadius_ = 0.0f;
  float fovDeg_ = 0.0f;
  float orbitAz_ = 0.0f, orbitEl_ = 0.0f;
  float distance_ = 0.0f;      // 0 = derive from bound radius
  TD_Vec3 target_ = {0.0f, 0.0f, 0.0f};
  bool haveTarget_ = false;    // false = use scene bounds center
  bool haveCenterPx_ = false;  // false = target center
  bool latched_ = false;       // auto-fit bounds captured
  bool explicitPose_ = false;  // lookAt() bypasses orbit resolution
  // scene bounds captured while latched_; consulted only in auto-fit mode.
  TD_Vec3 latchCenter_ = {0.0f, 0.0f, 0.0f};
  float latchRadius_ = 0.0f;

  friend class TD_Renderer;
};

#endif  // TD_CAMERA_H
