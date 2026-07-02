// SPDX-License-Identifier: MIT
#ifndef TD_SCENE_H
#define TD_SCENE_H

#include <stdint.h>

#include "TD_Camera.h"
#include "TD_Light.h"
#include "TD_Object.h"
#include "TD_RenderTarget.h"
#include "TD_Renderer.h"
#include "TD_Types.h"

// the front door: objects + camera + light + ambient, rendered in one call.
// camera and light are public value members with working defaults, so the
// minimal sketch is add-an-object, render, done.
//
// objects are user-owned and linked intrusively: add() stores a pointer and
// threads TD_Object::next_, so scene membership allocates nothing and has no
// capacity cap. one object belongs to at most one scene at a time (add() of an
// already-linked object is a no-op); an object must outlive its scene
// membership.
class TD_Scene {
 public:
  TD_Camera camera;  // scene.camera.setOrbit(...), auto-fit by default
  TD_Light light;    // default: upper-left key light, intensity 1.0

  void add(TD_Object &obj);
  void remove(TD_Object &obj);

  void setAmbient(float a) { ambient_ = a; }  // 0..1 shade floor; default 0.15
  float ambient() const { return ambient_; }

  void setRenderMode(TD_RenderMode m) { renderer_.setMode(m); }  // default TD_SHADED
  TD_RenderMode renderMode() const { return renderer_.mode(); }

  // render exactly one frame into the target (beginFrame..endFrame bracketed).
  // returns false on failure — reason in lastStatus() — with the target
  // completely untouched (validation, buffer growth, and camera resolution all
  // happen before the frame bracket opens).
  bool render(TD_RenderTarget &target);
  TD_Status lastStatus() const { return renderer_.lastStatus(); }

  // the engine underneath, for host tests and advanced use.
  TD_Renderer &renderer() { return renderer_; }

  // scene-list access for the renderer and tests.
  const TD_Object *firstObject() const { return head_; }

 private:
  TD_Object *head_ = nullptr;
  float ambient_ = 0.15f;
  TD_Renderer renderer_;

  friend class TD_Renderer;
};

#endif  // TD_SCENE_H
