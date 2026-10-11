// SPDX-License-Identifier: MIT
#ifndef TD_RENDERER_H
#define TD_RENDERER_H

#include <stdint.h>

#include "TD_Math.h"
#include "TD_Mesh.h"
#include "TD_MeshEdges.h"
#include "TD_RenderTarget.h"
#include "TD_Types.h"

class TD_Scene;
class TD_Camera;
struct TD_EdgeTable;          // internal/td_edge_table.h
struct TD_OutlineCacheEntry;  // TD_Renderer.cpp: cached per-mesh edge tables
struct TD_BakedEdgesEntry;    // TD_Renderer.cpp: baked tables handed in per mesh

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
// occlusion is painter's algorithm on per-face mean view depth (TD_OUTLINE
// adds a per-pixel depth test for its ink, against the frame's own fills):
// correct for
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

  // cel band count for TD_CEL: lit shades quantize to this many uniform
  // levels spanning [ambient, 1.0]. clamped to 2..TD_CEL_MAX_BANDS; default 4.
  void setCelBands(uint8_t n) {
    celBands_ = n < 2 ? 2 : (n > TD_CEL_MAX_BANDS ? TD_CEL_MAX_BANDS : n);
  }
  uint8_t celBands() const { return celBands_; }

  // cel ink style for TD_CEL's outline pass. default TD_CEL_INK_BLACK.
  void setCelInk(TD_CelInk ink) { celInk_ = (uint8_t)ink; }
  TD_CelInk celInk() const { return (TD_CelInk)celInk_; }

  // runtime winding flip for meshes wound clockwise-outward. default +1.
  void setCullSign(float s) { cullSign_ = s; }

  // TD_OUTLINE crease threshold, radians: dihedrals sharper than this ink as
  // drawn-on feature lines. default TD_OUTLINE_CREASE_DEFAULT (60 degrees,
  // converter parity). crease flags are baked into the cached edge tables at
  // build, so changing the angle marks every cached table stale.
  void setOutlineCrease(float radians);
  float outlineCrease() const { return outlineCrease_; }

  // optional setup-time build of the edge tables for every kept mesh in the
  // scene, so allocation failure is visible at init (the reserve() doctrine);
  // otherwise tables build lazily in the prepare stage of the first OUTLINE
  // frame. tables are renderer-owned and cached per mesh.
  bool prebuildOutline(const TD_Scene &scene);

  // drops the cached edge table for `mesh` (nullptr: all of them). call when
  // a mesh's topology changes or the mesh is freed — the cache key (pointer +
  // counts checksum) catches free-then-realloc reuse cheaply, but an
  // identical-shape rebuild at the same address wants an explicit invalidate.
  void invalidateOutline(const TD_Mesh *mesh = nullptr);

  // hands in a baked edge table for `mesh` (TD_MeshEdges, from
  // tools/bake_edges.cpp): while its counts match the mesh and its crease
  // matches outlineCrease(), it stands in for the built table — no table
  // allocation, no build scratch. otherwise the table builds as usual. the
  // arrays are borrowed and must outlive the renderer, like a mesh; the
  // registration is kept through releaseBuffers() and invalidateOutline(),
  // and a second call for the same mesh replaces it. false (nothing kept) if
  // the table's counts differ from the mesh's, an opp entry is out of range,
  // or its registry slot can't be allocated.
  bool setOutlineEdges(const TD_Mesh &mesh, const TD_MeshEdges &edges);

  // pre-size the working buffers in setup() so allocation failures surface at
  // init, not mid-run. sizing: 12 B/vertex + 17 B/face + ~108 B/object on
  // 32-bit MCUs (sizeof(ObjFrame): two 3x3 matrices, a translation, and
  // per-object attributes), summed over the largest scene. TD_OUTLINE adds
  // 4 B per target pixel (its depth buffer), sized in the prepare stage from
  // the target, not here.
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
  // one draw record per visible face (and, under TD_OUTLINE, per back face of
  // a fill-less object, for its ink), all objects pooled, painter-sorted by
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
    uint32_t faceBase;    // this object's offset into the per-face buffers
                          //   (OUTLINE's neighbor-facing lookups need it)
    uint16_t baseColor, objectIdx;
    uint8_t material;     // whole-object TD_Material override
    bool ink;             // false: no edge of this object is ever drawn
    bool fill;            // false: no face of this object fills or records depth
    uint8_t group;        // its line group, 0 for none
  };

  // working buffers: exact-fit high-water realloc, pointers only reassigned
  // on success (a failed grow leaves prior buffers valid and render() returns
  // TD_ERR_ALLOC having drawn nothing).
  float *sx_ = nullptr, *sy_ = nullptr, *sz_ = nullptr;  // per-vertex screen x/y, view z
  uint32_t capV_ = 0;
  float *faceZ_ = nullptr;   // per-face mean depth (sentinel on near-skip)
  DrawRec *rec_ = nullptr;
  uint8_t *faceFront_ = nullptr;  // per-face facing bits; written only under
                                  //   TD_OUTLINE (silhouette classification)
  uint32_t capF_ = 0;
  ObjFrame *objs_ = nullptr;
  uint16_t capObj_ = 0;
  // TD_OUTLINE depth buffer: the nearest fill's view depth per target pixel,
  // reset every outline frame; the ink pass tests each edge pixel against it.
  float *depth_ = nullptr;
  uint32_t capDepth_ = 0;
  // beside it, only in a frame with a line group: per pixel, the line group
  // of the fill that holds that depth, 0 for none or for the background.
  uint8_t *groups_ = nullptr;
  uint32_t capGroups_ = 0;

  // TD_OUTLINE edge-table cache: one entry per distinct mesh, keyed on the
  // mesh pointer plus a (nverts, nfaces, faceVertCount) checksum. built in
  // the prepare stage (or prebuildOutline), freed by releaseBuffers() /
  // invalidateOutline().
  TD_OutlineCacheEntry *outlineCache_ = nullptr;
  uint16_t outlineCount_ = 0, outlineCap_ = 0;
  // baked tables handed in by setOutlineEdges, one per mesh; freed only by
  // the destructor.
  TD_BakedEdgesEntry *baked_ = nullptr;
  uint16_t bakedCount_ = 0;
  float outlineCrease_ = TD_OUTLINE_CREASE_DEFAULT;

  uint8_t mode_ = TD_SHADED;
  uint8_t celBands_ = 4;
  uint8_t celInk_ = TD_CEL_INK_BLACK;
  // cel latches, valid only inside drawFrame: set per frame before the
  // painter loop and per face before its fill; never carried across frames.
  bool celFrameOn_ = false;
  bool celSpanOn_ = false;
  float celAmbient_ = 0.0f;
  // outline fill latches, valid only inside drawFrame: while depthOn_ the
  // fill interpolant is view depth, written per pixel, and the shade that
  // crosses the seam is the flat depthFlat_. while groupsOn_ (the frame has
  // a line group) each depth written stamps depthGroup_.
  bool depthOn_ = false;
  float depthFlat_ = 0.0f;
  bool groupsOn_ = false;
  uint8_t depthGroup_ = 0;
  float cullSign_ = 1.0f;
  uint8_t status_ = TD_OK;
  int16_t tw_ = 0, th_ = 0;  // target dims cached for the frame
  uint32_t lastDrawn_ = 0, lastSkipped_ = 0;

  bool ensureCapacity(uint32_t nv, uint32_t nf, uint16_t nobj);
  bool ensureDepth(uint32_t npix);
  bool ensureGroups(uint32_t npix);
  static int cmpFarFirst(const void *a, const void *b);

  // the fallible prologue (validate, size, resolve camera) and the infallible
  // body (transform, sort, draw) of one pass. render() runs prepare() before
  // opening the frame bracket; renderPass() runs both inside the caller's
  // bracket.
  bool prepare(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target);
  void drawFrame(const TD_Scene &scene, const TD_Camera &camera, TD_RenderTarget &target);

  // rasterization: scanline fill sampling pixel centers, half-open
  // right/bottom (adjacent triangles neither crack nor double-draw); spans
  // cross the seam once each, except that cel banding may split a geometric
  // span into several flat sub-spans, each crossing once; Bresenham lines
  // clamp endpoints to the TD_GUARD_BAND before integer conversion and keep
  // the per-pixel bounds funnel (the one place pixels could go out of
  // bounds).
  void fillTri(TD_RenderTarget &t, float x0, float y0, float s0, float x1, float y1, float s1,
               float x2, float y2, float s2);
  void fillSpan(TD_RenderTarget &t, int y, float xl, float xr, float sl, float sr);
  void celFillSpan(TD_RenderTarget &t, int y, float xl, float xr, float sl, float sr);
  void depthFillSpan(TD_RenderTarget &t, int y, float xl, float xr, float zl, float zr);
  void drawLineShade(TD_RenderTarget &t, float x0, float y0, float x1, float y1, float shade);
  void drawLineDepth(TD_RenderTarget &t, float x0, float y0, float z0, float x1, float y1,
                     float z1, float tol, uint8_t group);
  void outlineLoop(TD_RenderTarget &t, const ObjFrame &of, uint16_t loopStart,
                   uint16_t loopCount, float shade);

  // TD_OUTLINE internals: cache lookup/build, and the classified ink pass —
  // one face's loop edges drawn at 2.0 iff boundary, crease, or silhouette
  // (the neighbor across the edge is not known front-facing; a back face, a
  // fill-less object's, draws its boundary and crease edges only), each pixel
  // depth-tested against the frame's fills. bakedEdges() is the handed-in
  // table for `mesh` while it applies, else nullptr.
  bool ensureEdgeTable(const TD_Mesh &mesh);
  const TD_EdgeTable *findEdgeTable(const TD_Mesh &mesh) const;
  const TD_MeshEdges *bakedEdges(const TD_Mesh &mesh) const;
  void outlineClassifiedLoop(TD_RenderTarget &t, const ObjFrame &of, uint16_t face);
};

// the pure cel quantizers, standalone like td_ditherOn: the renderer calls
// exactly these, so the host tests pin the render path's own band math.
// expression shapes are golden-pinned — treat rewrites as behavioral changes.

// shade threshold between cel band k and k+1 among `bands` uniform levels
// spanning [ambient, 1.0]. thresholds sit at level midpoints. callers pass
// 0 <= k <= bands-2, bands >= 2, ambient sufficiently below 1: within a few
// ulps of 1.0 a threshold can round to exactly 1.0f and the top band becomes
// unreachable — a harmless degradation, never UB.
float td_celThresh(int k, float ambient, uint8_t bands);

// cel band index of a lit shade: the number of thresholds the shade exceeds.
// monotone in shade; NaN maps to band 0.
int td_celBand(float shade, float ambient, uint8_t bands);

// output level of a cel band: band 0 is exactly ambient, the top band
// exactly 1.0f.
float td_celLevel(int band, float ambient, uint8_t bands);

#endif  // TD_RENDERER_H
