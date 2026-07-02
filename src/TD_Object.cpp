// SPDX-License-Identifier: MIT

#include "TD_Object.h"

// world-transform and display attributes get working defaults from the
// header's member initializers (origin, no rotation, scale 1, white, lit,
// visible); construction only binds the mesh reference.
TD_Object::TD_Object(const TD_Mesh &mesh) : mesh_(&mesh) {}

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
