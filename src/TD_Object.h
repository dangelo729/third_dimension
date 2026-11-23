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
// supported — many objects may reference one mesh. an object still in a
// scene when that scene is destroyed keeps its link and cannot join another
// scene: remove() it before the scene goes.
class TD_Object {
 public:
  explicit TD_Object(const TD_Mesh &mesh);

  // copying takes the mesh and every attribute but never the scene link: a
  // copy starts in no scene, and an object assigned to (obj =
  // TD_Object(otherMesh), to give it another mesh) keeps its own place in
  // its scene.
  TD_Object(const TD_Object &other);
  TD_Object &operator=(const TD_Object &other);

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
  // false: the object still fills — under TD_OUTLINE it still records its
  // depth, so it hides what lies behind it — but none of its edges is ever
  // drawn as a line, in any mode. an occluder, like the dark back of a
  // mouth. true by default.
  void setInk(bool on);
  // false: the object fills nothing and records no depth, in any mode, so it
  // hides nothing; its edges draw as the mode draws lines. under TD_OUTLINE
  // its back faces' crease and boundary edges draw too, so the far side of a
  // glass shows through the near. true by default.
  void setFill(bool on);
  // under TD_OUTLINE, objects sharing a line group (1..255) draw as one
  // outline: a member's line draws only where it borders the background or
  // a fill outside the group, never against the group's own fills — so
  // parts sunk into each other, like cheeks on a head, show only the
  // contour of their union: no member's rim across another, no contour
  // through one, and no crease inside any. against fills outside the group
  // its lines depth-test as any line does. 0, the default, is no group; a
  // frame with a group costs 1 B/pixel more of outline buffer.
  void setInkGroup(uint8_t group);

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
  bool ink() const { return ink_; }
  bool fill() const { return fill_; }
  uint8_t inkGroup() const { return inkGroup_; }
  const TD_Mesh &mesh() const { return *mesh_; }

 private:
  const TD_Mesh *mesh_;
  float posX_ = 0.0f, posY_ = 0.0f, posZ_ = 0.0f;
  float rotX_ = 0.0f, rotY_ = 0.0f, rotZ_ = 0.0f;
  float scale_ = 1.0f;
  uint16_t color_ = 0xFFFF;
  uint8_t material_ = TD_MATERIAL_LIT;
  bool visible_ = true;
  bool ink_ = true;
  bool fill_ = true;
  uint8_t inkGroup_ = 0;
  TD_Object *next_ = nullptr;  // intrusive scene-list link

  friend class TD_Scene;
  friend class TD_Renderer;
};

#endif  // TD_OBJECT_H
