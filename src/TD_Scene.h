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
//
// lights: the built-in key light plus user-owned fills added via addLight,
// linked intrusively like objects.
class TD_Scene {
 public:
  TD_Camera camera;  // scene.camera.setOrbit(...), auto-fit by default
  TD_Light light;    // default: upper-left key light, intensity 1.0

  void add(TD_Object &obj);
  void remove(TD_Object &obj);

  // extra directional lights beyond scene.light (the key light), user-owned
  // and linked intrusively like objects: addLight() stores a pointer and
  // threads TD_Light::next_, so light membership allocates nothing and has
  // no capacity cap. shading sums scene.light first, then added lights in
  // add order. one light belongs to at most one scene at a time (addLight()
  // of an already-linked light is a no-op); a light must outlive its scene
  // membership. scene.light itself is never in this list and cannot be added.
  void addLight(TD_Light &l);
  void removeLight(TD_Light &l);

  void setAmbient(float a) { ambient_ = a; }  // 0..1 shade floor; default 0.15
  float ambient() const { return ambient_; }

  void setRenderMode(TD_RenderMode m) { renderer_.setMode(m); }  // default TD_SHADED
  TD_RenderMode renderMode() const { return renderer_.mode(); }

  void setCelBands(uint8_t n) { renderer_.setCelBands(n); }  // TD_CEL band count; default 4
  uint8_t celBands() const { return renderer_.celBands(); }
  void setCelInk(TD_CelInk ink) { renderer_.setCelInk(ink); }  // TD_CEL ink; default black
  TD_CelInk celInk() const { return renderer_.celInk(); }

  // TD_OUTLINE crease threshold, radians (default TD_OUTLINE_CREASE_DEFAULT,
  // 60 degrees — converter parity); changing it marks the cached edge tables
  // stale. prebuildOutline() builds the tables for this scene's meshes at
  // setup, where an allocation failure is visible (the reserve() doctrine);
  // otherwise they build lazily on the first TD_OUTLINE frame.
  void setOutlineCrease(float radians) { renderer_.setOutlineCrease(radians); }
  float outlineCrease() const { return renderer_.outlineCrease(); }
  bool prebuildOutline() { return renderer_.prebuildOutline(*this); }
  // a baked edge table for `mesh` (tools/bake_edges.cpp), used in place of
  // building one while it matches the mesh and the crease — see
  // TD_Renderer::setOutlineEdges.
  bool setOutlineEdges(const TD_Mesh &mesh, const TD_MeshEdges &edges) {
    return renderer_.setOutlineEdges(mesh, edges);
  }

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

  // light-list access for the renderer and tests.
  const TD_Light *firstExtraLight() const { return lightHead_; }

 private:
  TD_Object *head_ = nullptr;
  TD_Light *lightHead_ = nullptr;
  float ambient_ = 0.15f;
  TD_Renderer renderer_;

  friend class TD_Renderer;
};

#endif  // TD_SCENE_H
