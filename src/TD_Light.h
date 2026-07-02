// SPDX-License-Identifier: MIT
#ifndef TD_LIGHT_H
#define TD_LIGHT_H

#include "TD_Camera.h"
#include "TD_Math.h"

// one directional light, stored as a world-space unit vector pointing from
// surfaces toward the light. a corner is lit when dot(n_world, L) > 0; the
// shade formula is clamp(ambient + (1 - ambient) * diff * intensity).
// defaults (az 0.7, el 0.45: an upper-left key light) mean a default scene
// shades correctly with zero light code.
class TD_Light {
 public:
  // default built through setDirection so the stored vector can't drift
  // from the documented az/el pair.
  TD_Light() { setDirection(0.7f, 0.45f); }

  // azimuth orbits world Y (az 0 = from the -Z direction: a headlight for
  // the default camera); elevation tilts the light up. the exact formula:
  // L = (-sin(az)*cos(el), sin(el), -cos(az)*cos(el)).
  void setDirection(float azimuth, float elevation) {
    float ce = cosf(elevation);
    dir_ = {-sinf(azimuth) * ce, sinf(elevation), -cosf(azimuth) * ce};
  }

  void setDirectionVector(float x, float y, float z) { dir_ = tdNormalize({x, y, z}); }

  // light from the camera, for any pose.
  void setHeadlight(const TD_Camera &cam) { dir_ = tdScale(cam.forward(), -1.0f); }

  void setIntensity(float i) { intensity_ = i; }  // 1 = normal, 0 = ambient only

  TD_Vec3 direction() const { return dir_; }
  float intensity() const { return intensity_; }

 private:
  TD_Vec3 dir_;
  float intensity_ = 1.0f;
};

#endif  // TD_LIGHT_H
