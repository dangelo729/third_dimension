// SPDX-License-Identifier: MIT
#ifndef TD_RENDERER_H
#define TD_RENDERER_H

#include <stdint.h>

#include "TD_Math.h"
#include "TD_Mesh.h"
#include "TD_RenderTarget.h"
#include "TD_Types.h"

class TD_Scene;
class TD_Camera;

// the engine: transform -> project -> per-triangle back-face cull -> one
// global painter sort across all objects -> Gouraud shade -> scanline spans
// across the render-target seam. sketches drive it through
// TD_Scene::render(); it stays public for the host test harness and for
// compositing (multiple scenes sharing one renderer's buffers, manual frame
// composition via renderPass).
//
// everything is instance state, no globals. one renderer per thread;
// render() is not ISR-safe and not reentrant; two renderers, two displays,
// or one scene rendered to several targets are all supported.
//
// occlusion is painter's algorithm on per-face mean view depth: correct for
// non-interpenetrating objects with depth separation and reasonably
// tessellated faces (interpenetration and occlusion cycles have no correct
// draw order; large ground-plane faces mis-sort against small nearby
// objects — subdivide them). the sort is deterministic with full tie-breaks
// so output is a pure function of the input on every platform.
class TD_Renderer {
 public:
  TD_Renderer();
  ~TD_Renderer();
  TD_Renderer(const TD_Renderer &) = delete;
  TD_Renderer &operator=(const TD_Renderer &) = delete;

  void setMode(TD_RenderMode m) { mode_ = m; }
  TD_RenderMode mode() const { return (TD_RenderMode)mode_; }

  // runtime winding flip for meshes wound clockwise-outward. default +1.
  void setCullSign(float s) { cullSign_ = s; }

  // pre-size the working buffers in setup() so allocation failures surface at
  // init, not mid-run. sizing: 12 B/vertex + 16 B/face + ~104 B/object on
  // 32-bit MCUs (sizeof(ObjFrame): two 3x3 matrices, a translation, and
  // per-object attributes), summed over the largest scene.
  bool reserve(uint32_t maxVerts, uint32_t maxFaces, uint16_t maxObjects);
  void releaseBuffers();

  // one complete frame: brackets target.beginFrame()/endFrame() around a
  // render pass. everything fallible (dimension checks, buffer growth, camera
  // intent resolution) runs before the bracket opens, so a false return
  // leaves the target completely untouched — not even auto-cleared — with the
  // reason in lastStatus(). the camera is taken by non-const reference because
  // resolving its framing intent mutates it (auto-fit latching, orbit pose,
  // resolved focal).
  bool render(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target);

  // the unbracketed pass, for compositing several scenes/passes into one
  // frame: caller runs target.beginFrame(), N renderPass() calls, then
  // target.endFrame() (diffusion targets must quantize exactly once).
  bool renderPass(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target);

  TD_Status lastStatus() const { return (TD_Status)status_; }
  uint32_t lastDrawCount() const { return lastDrawn_; }        // faces drawn
  uint32_t lastNearSkipCount() const { return lastSkipped_; }  // faces near-skipped

 private:
  // one draw record per visible face, all objects pooled, painter-sorted by
  // (grp asc, z desc, obj, face) — the trailing tie-breaks make qsort output
  // deterministic across libcs (multi-object scenes make exact z ties common:
  // instances of one mesh, coplanar tiles).
  struct DrawRec {
    float z;              // mean view-space depth over the face loop (decal-biased)
    uint16_t obj, face;   // scene object index, mesh-local face index
    uint8_t grp;          // 0 = body face, 1 = decal (drawn after all body faces)
  };

  // per-object per-frame transform cache.
  struct ObjFrame {
    const TD_Mesh *mesh;
    TD_Mat3 M;            // view rotation * object rotation, scaled (points)
    TD_Mat3 N;            // object rotation only (normals; world-space shading)
    TD_Vec3 T;            // view rotation * (object position - eye)
    float scale;          // for scaling the decal depth bias
    uint32_t vertBase;    // this object's offset into the shared vertex buffers
    uint16_t baseColor, objectIdx;
    uint8_t material;     // whole-object TD_Material override
  };

  // working buffers: exact-fit high-water realloc, pointers only reassigned
  // on success (a failed grow leaves prior buffers valid and render() returns
  // TD_ERR_ALLOC having drawn nothing).
  float *sx_ = nullptr, *sy_ = nullptr, *sz_ = nullptr;  // per-vertex screen x/y, view z
  uint32_t capV_ = 0;
  float *faceZ_ = nullptr;   // per-face mean depth (sentinel on near-skip)
  DrawRec *rec_ = nullptr;
  uint32_t capF_ = 0;
  ObjFrame *objs_ = nullptr;
  uint16_t capObj_ = 0;

  uint8_t mode_ = TD_SHADED;
  float cullSign_ = 1.0f;
  uint8_t status_ = TD_OK;
  int16_t tw_ = 0, th_ = 0;  // target dims cached for the frame
  uint32_t lastDrawn_ = 0, lastSkipped_ = 0;

  bool ensureCapacity(uint32_t nv, uint32_t nf, uint16_t nobj);
  static int cmpFarFirst(const void *a, const void *b);

  // the fallible prologue (validate, size, resolve camera) and the infallible
  // body (transform, sort, draw) of one pass. render() runs prepare() before
  // opening the frame bracket; renderPass() runs both inside the caller's
  // bracket.
  bool prepare(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target);
  void drawFrame(const TD_Scene &scene, const TD_Camera &camera, TD_RenderTarget &target);

  // rasterization: scanline fill sampling pixel centers, half-open
  // right/bottom (adjacent triangles neither crack nor double-draw); spans
  // cross the seam once each; Bresenham lines clamp endpoints to the
  // TD_GUARD_BAND before integer conversion and keep the per-pixel bounds
  // funnel (the one place pixels could go out of bounds).
  void fillTri(TD_RenderTarget &t, float x0, float y0, float s0, float x1, float y1, float s1,
               float x2, float y2, float s2);
  void fillSpan(TD_RenderTarget &t, int y, float xl, float xr, float sl, float sr);
  void drawLineShade(TD_RenderTarget &t, float x0, float y0, float x1, float y1, float shade);
  void outlineLoop(TD_RenderTarget &t, const ObjFrame &of, uint16_t loopStart,
                   uint16_t loopCount, float shade);
};

#endif  // TD_RENDERER_H
