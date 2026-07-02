// SPDX-License-Identifier: MIT
#ifndef TD_OBJECT_H
#define TD_OBJECT_H

#include <stdint.h>

#include "TD_Mesh.h"
#include "TD_Types.h"

class TD_Scene;
class TD_Renderer;

// one instance of a mesh in a scene: a mesh reference plus a world transform
// and per-object display attributes. objects are user-owned (declare them at
// global scope like any Arduino peripheral object); the scene links them
// through an intrusive pointer, so adding an object allocates nothing.
// an object belongs to at most one scene at a time. mesh sharing is
// supported — many objects may reference one mesh.
class TD_Object {
 public:
  explicit TD_Object(const TD_Mesh &mesh);

  void setPosition(float x, float y, float z);  // world units
  // radians, applied v' = Rx*Ry*Rz*v — Z (roll) first, then Y (yaw = the
  // turntable spin), then X (pitch). this order is an API commitment.
  void setRotation(float rx, float ry, float rz);
  // uniform only: uniform scale is the case where normals transform by the
  // rotation alone (no renormalization in the hot loop).
  void setScale(float s);
  void setColor(uint16_t rgb565);      // base color; color targets only
  // whole-object override. TD_MATERIAL_LIT (the default) defers to the
  // mesh's per-face fattr; BLACK/WHITE override shading for every face but
  // leave decal draw ordering intact.
  void setMaterial(TD_Material m);
  void setVisible(bool v);

  float x() const { return posX_; }
  float y() const { return posY_; }
  float z() const { return posZ_; }
  float rotX() const { return rotX_; }
  float rotY() const { return rotY_; }
  float rotZ() const { return rotZ_; }
  float scale() const { return scale_; }
  uint16_t color() const { return color_; }
  TD_Material material() const { return (TD_Material)material_; }
  bool visible() const { return visible_; }
  const TD_Mesh &mesh() const { return *mesh_; }

 private:
  const TD_Mesh *mesh_;
  float posX_ = 0.0f, posY_ = 0.0f, posZ_ = 0.0f;
  float rotX_ = 0.0f, rotY_ = 0.0f, rotZ_ = 0.0f;
  float scale_ = 1.0f;
  uint16_t color_ = 0xFFFF;
  uint8_t material_ = TD_MATERIAL_LIT;
  bool visible_ = true;
  TD_Object *next_ = nullptr;  // intrusive scene-list link

  friend class TD_Scene;
  friend class TD_Renderer;
};

#endif  // TD_OBJECT_H
