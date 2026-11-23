// SPDX-License-Identifier: MIT

#include "TD_Object.h"

// world-transform and display attributes get working defaults from the
// header's member initializers (origin, no rotation, scale 1, white, lit,
// visible); construction only binds the mesh reference.
TD_Object::TD_Object(const TD_Mesh &mesh) : mesh_(&mesh) {}

// next_ keeps its initializer: a copy starts in no scene.
TD_Object::TD_Object(const TD_Object &other) : mesh_(other.mesh_) { *this = other; }

// every member but next_, which stays this object's own scene link.
TD_Object &TD_Object::operator=(const TD_Object &other) {
  mesh_ = other.mesh_;
  posX_ = other.posX_;
  posY_ = other.posY_;
  posZ_ = other.posZ_;
  rotX_ = other.rotX_;
  rotY_ = other.rotY_;
  rotZ_ = other.rotZ_;
  scale_ = other.scale_;
  color_ = other.color_;
  material_ = other.material_;
  visible_ = other.visible_;
  ink_ = other.ink_;
  fill_ = other.fill_;
  inkGroup_ = other.inkGroup_;
  return *this;
}

void TD_Object::setPosition(float x, float y, float z) {
  posX_ = x;
  posY_ = y;
  posZ_ = z;
}

void TD_Object::setRotation(float rx, float ry, float rz) {
  rotX_ = rx;
  rotY_ = ry;
  rotZ_ = rz;
}

void TD_Object::setScale(float s) { scale_ = s; }

void TD_Object::setColor(uint16_t rgb565) { color_ = rgb565; }

void TD_Object::setMaterial(TD_Material m) { material_ = (uint8_t)m; }

void TD_Object::setVisible(bool v) { visible_ = v; }

void TD_Object::setInk(bool on) { ink_ = on; }
void TD_Object::setFill(bool on) { fill_ = on; }
void TD_Object::setInkGroup(uint8_t group) { inkGroup_ = group; }
