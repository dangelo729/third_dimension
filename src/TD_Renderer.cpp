// SPDX-License-Identifier: MIT
#include "TD_Renderer.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "TD_Camera.h"
#include "TD_Config.h"
#include "TD_Scene.h"
#include "internal/td_edge_table.h"

// the engine. one geometry pipeline serves every render target: transform ->
// project -> per-triangle back-face cull -> one global painter sort -> Gouraud
// shade -> scanline spans across the render-target seam.
//
// the floating-point expression shapes in the transform, projection, and
// shading blocks below are pinned by bit-exact golden frame tests. translation
// is added after the full rotation dot products; projection multiplies before
// it divides; the shade formula evaluates left to right, and each added
// light's term ((1.0f - ambient) * d * el->intensity_, summed after the key
// light in add order) is a pinned shape too. an algebraically equivalent
// rewrite is a behavioral change here.

// one cached TD_OUTLINE edge table. the (nverts, nfaces, faceVertCount)
// checksum beside the pointer catches free-then-realloc mesh reuse cheaply;
// meshes that share topology arrays still get separate (identical) tables —
// a cleverer key isn't worth its bookkeeping at 3 B per loop slot.
struct TD_OutlineCacheEntry {
  const TD_Mesh *mesh;
  uint16_t nv, nf, nloop;
  TD_EdgeTable table;
};

// facing bits (faceFront_), meaningful only while drawing a TD_OUTLINE frame.
// UNKNOWN covers faces the record loop never classified (near-skipped, or a
// decal's slot): the neighbor isn't drawn, so an edge against it inks as the
// boundary of drawn geometry.
enum : uint8_t {
  TD_FACING_BACK = 0,
  TD_FACING_FRONT = 1,
  TD_FACING_UNKNOWN = 2,
};

TD_Renderer::TD_Renderer() {
  // all members carry in-class defaults; working buffers stay null until the
  // first reserve()/render() grows them.
}

TD_Renderer::~TD_Renderer() {
  // working buffers are renderer-owned; releasing here keeps the
  // declare-a-global Arduino idiom leak-free.
  releaseBuffers();
}

bool TD_Renderer::reserve(uint32_t maxVerts, uint32_t maxFaces, uint16_t maxObjects) {
  // setup-time pre-allocation so failure is visible at init instead of
  // mid-run: delegates to the same exact-fit grower the render path uses.
  // sizing: 12 B/vertex + 17 B/face + ~108 B/object over the largest scene.
  return ensureCapacity(maxVerts, maxFaces, maxObjects);
}

void TD_Renderer::releaseBuffers() {
  // returns all working memory between scenes; the next reserve()/render()
  // regrows lazily through ensureCapacity. the per-vertex arrays are one
  // allocation — sy_/sz_ alias into sx_'s block, so only sx_ is freed.
  // safe to call at any time between frames (free(nullptr) is a no-op).
  free(sx_);
  sx_ = sy_ = sz_ = nullptr;
  capV_ = 0;
  free(faceZ_);
  faceZ_ = nullptr;
  free(rec_);
  rec_ = nullptr;
  free(faceFront_);
  faceFront_ = nullptr;
  capF_ = 0;
  free(objs_);
  objs_ = nullptr;
  capObj_ = 0;
  free(depth_);
  depth_ = nullptr;
  capDepth_ = 0;
  free(groups_);
  groups_ = nullptr;
  capGroups_ = 0;
  invalidateOutline(nullptr);
  free(outlineCache_);
  outlineCache_ = nullptr;
  outlineCap_ = 0;
}

bool TD_Renderer::ensureCapacity(uint32_t nv, uint32_t nf, uint16_t nobj) {
  // exact-fit high-water growth: a block is replaced only when the request
  // exceeds its capacity, and then to exactly the requested size — the
  // footprint is a deterministic function of the largest scene seen. every
  // replacement is allocated into a temporary and the members reassigned only
  // on success, so a failed grow leaves all prior buffers valid and the frame
  // aborts having drawn nothing.
  if (nv > capV_) {
    if (nv > 0x0FFFFFFFu) return false;  // 3*nv floats must fit size_t on 32-bit MCUs
    float *p = (float *)malloc((size_t)nv * 3u * sizeof(float));
    if (!p) return false;
    free(sx_);
    sx_ = p;
    sy_ = p + nv;
    sz_ = p + 2u * nv;
    capV_ = nv;
  }
  if (nf > capF_) {
    if (nf > 0x0FFFFFFFu) return false;
    float *pz = (float *)malloc((size_t)nf * sizeof(float));
    if (!pz) return false;
    DrawRec *pr = (DrawRec *)malloc((size_t)nf * sizeof(DrawRec));
    if (!pr) {
      free(pz);
      return false;
    }
    uint8_t *pf = (uint8_t *)malloc((size_t)nf);
    if (!pf) {
      free(pz);
      free(pr);
      return false;
    }
    free(faceZ_);
    faceZ_ = pz;
    free(rec_);
    rec_ = pr;
    free(faceFront_);
    faceFront_ = pf;
    capF_ = nf;
  }
  if (nobj > capObj_) {
    ObjFrame *po = (ObjFrame *)malloc((size_t)nobj * sizeof(ObjFrame));
    if (!po) return false;
    free(objs_);
    objs_ = po;
    capObj_ = nobj;
  }
  return true;
}

// scene-list filter: an object takes part in the frame when it is visible and
// its mesh has geometry. empty meshes (failed primitive builds) render as a
// safe no-op through this check alone.
static inline bool tdKeepObject(const TD_Object &o) {
  const TD_Mesh &m = o.mesh();
  return o.visible() && m.nverts != 0 && m.ntris != 0 && m.nfaces != 0;
}

// the TD_OUTLINE depth buffer: one float per target pixel, grown to the
// largest target seen and reassigned only on success, like the rest.
bool TD_Renderer::ensureDepth(uint32_t npix) {
  if (npix <= capDepth_) return true;
  if (npix > 0x0FFFFFFFu) return false;
  float *p = (float *)malloc((size_t)npix * sizeof(float));
  if (!p) return false;
  free(depth_);
  depth_ = p;
  capDepth_ = npix;
  return true;
}

// the TD_OUTLINE line-group buffer beside it: one byte per target pixel,
// grown the same way, allocated only for a frame with a line group.
bool TD_Renderer::ensureGroups(uint32_t npix) {
  if (npix <= capGroups_) return true;
  if (npix > 0x0FFFFFFFu) return false;
  uint8_t *p = (uint8_t *)malloc((size_t)npix);
  if (!p) return false;
  free(groups_);
  groups_ = p;
  capGroups_ = npix;
  return true;
}

// ---- TD_OUTLINE edge-table cache -------------------------------------------

void TD_Renderer::setOutlineCrease(float radians) {
  // crease flags are baked into the tables at build, so a changed angle
  // makes every cached table stale; an equal set is a no-op.
  if (radians == outlineCrease_) return;
  outlineCrease_ = radians;
  invalidateOutline(nullptr);
}

void TD_Renderer::invalidateOutline(const TD_Mesh *mesh) {
  if (mesh == nullptr) {
    for (uint16_t i = 0; i < outlineCount_; ++i) outlineCache_[i].table.release();
    outlineCount_ = 0;
    return;
  }
  for (uint16_t i = 0; i < outlineCount_; ++i) {
    if (outlineCache_[i].mesh == mesh) {
      outlineCache_[i].table.release();
      outlineCache_[i] = outlineCache_[outlineCount_ - 1];
      --outlineCount_;
      return;
    }
  }
}

const TD_EdgeTable *TD_Renderer::findEdgeTable(const TD_Mesh &mesh) const {
  // linear scan: scenes hold a handful of distinct meshes, and the draw loop
  // memoizes per record run.
  for (uint16_t i = 0; i < outlineCount_; ++i) {
    const TD_OutlineCacheEntry &e = outlineCache_[i];
    if (e.mesh == &mesh && e.nv == mesh.nverts && e.nf == mesh.nfaces &&
        e.nloop == mesh.faceVertCount)
      return &e.table;
  }
  return nullptr;
}

bool TD_Renderer::ensureEdgeTable(const TD_Mesh &mesh) {
  for (uint16_t i = 0; i < outlineCount_; ++i) {
    TD_OutlineCacheEntry &e = outlineCache_[i];
    if (e.mesh != &mesh) continue;
    if (e.nv == mesh.nverts && e.nf == mesh.nfaces && e.nloop == mesh.faceVertCount)
      return true;
    // same address, different counts: the storage was reused for another
    // mesh — rebuild in place. a failed build drops the entry so the cache
    // never carries a stale table.
    e.table.release();
    if (!e.table.build(mesh, outlineCrease_)) {
      outlineCache_[i] = outlineCache_[outlineCount_ - 1];
      --outlineCount_;
      return false;
    }
    e.nv = mesh.nverts;
    e.nf = mesh.nfaces;
    e.nloop = mesh.faceVertCount;
    return true;
  }
  if (outlineCount_ == outlineCap_) {
    // exact-fit growth, one entry at a time: new distinct meshes are rare.
    uint16_t cap = (uint16_t)(outlineCap_ + 1);
    TD_OutlineCacheEntry *p =
        (TD_OutlineCacheEntry *)realloc(outlineCache_, (size_t)cap * sizeof(TD_OutlineCacheEntry));
    if (!p) return false;
    outlineCache_ = p;
    outlineCap_ = cap;
  }
  TD_OutlineCacheEntry &e = outlineCache_[outlineCount_];
  e.mesh = &mesh;
  e.nv = mesh.nverts;
  e.nf = mesh.nfaces;
  e.nloop = mesh.faceVertCount;
  e.table = TD_EdgeTable{};
  if (!e.table.build(mesh, outlineCrease_)) return false;
  ++outlineCount_;
  return true;
}

bool TD_Renderer::prebuildOutline(const TD_Scene &scene) {
  for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
    if (!tdKeepObject(*o) || !o->ink()) continue;  // an inkless object needs no table
    if (!ensureEdgeTable(o->mesh())) return false;
  }
  return true;
}

bool TD_Renderer::render(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target) {
  // one complete frame is exactly one beginFrame..endFrame bracket around
  // one pass. everything fallible runs in prepare() before the bracket
  // opens, so a failed render leaves the target completely untouched — not
  // even auto-cleared (auto-clear is the built-in targets' own beginFrame
  // behavior, never the renderer's).
  if (!prepare(scene, camera, target)) return false;
  if (!target.beginFrame()) {
    status_ = TD_ERR_NO_TARGET;
    return false;
  }
  drawFrame(scene, camera, target);
  // endFrame always closes an opened bracket: diffusion targets quantize
  // exactly once per frame.
  target.endFrame();
  return true;
}

bool TD_Renderer::renderPass(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target) {
  // the caller owns the frame bracket; a false return means nothing was
  // drawn into it (the reason is in lastStatus()).
  if (!prepare(scene, camera, target)) return false;
  drawFrame(scene, camera, target);
  return true;
}

bool TD_Renderer::prepare(const TD_Scene &scene, TD_Camera &camera, TD_RenderTarget &target) {
  lastDrawn_ = 0;
  lastSkipped_ = 0;

  // everything that can fail happens here, before any pixel is touched.
  int16_t tw = target.width(), th = target.height();
  if (tw <= 0 || th <= 0) {
    status_ = TD_ERR_NO_TARGET;
    return false;
  }
  tw_ = tw;
  th_ = th;

  uint32_t totalV = 0, totalF = 0, nObj = 0;
  for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
    if (!tdKeepObject(*o)) continue;
    totalV += o->mesh().nverts;  // uint32 sums: scene totals may exceed u16
    totalF += o->mesh().nfaces;
    ++nObj;
  }
  if (nObj == 0) {
    status_ = TD_OK;  // an empty scene renders nothing, successfully
    return true;
  }
  if (nObj > 0xFFFFu || !ensureCapacity(totalV, totalF, (uint16_t)nObj)) {
    status_ = TD_ERR_ALLOC;
    return false;
  }

  // TD_OUTLINE edge tables for every kept mesh and the depth buffer for this
  // target, still before any pixel: a failed build aborts with the target
  // untouched, exactly like the working buffers (allocate-before-any-pixel
  // contract). cached tables are free.
  if (mode_ == TD_OUTLINE) {
    for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
      if (!tdKeepObject(*o) || !o->ink()) continue;  // an inkless object needs no table
      if (!ensureEdgeTable(o->mesh())) {
        status_ = TD_ERR_ALLOC;
        return false;
      }
    }
    if (!ensureDepth((uint32_t)tw_ * (uint32_t)th_)) {
      status_ = TD_ERR_ALLOC;
      return false;
    }
    // the line-group buffer, for a frame with a line group.
    bool grouped = false;
    for (const TD_Object *o = scene.firstObject(); o; o = o->next_)
      grouped = grouped || (tdKeepObject(*o) && o->inkGroup() != 0);
    if (grouped && !ensureGroups((uint32_t)tw_ * (uint32_t)th_)) {
      status_ = TD_ERR_ALLOC;
      return false;
    }
  }

  // scene bound sphere for camera auto-fit: AABB center of the per-object
  // world spheres (position, |scale| * mesh bound radius), then the tightest
  // radius around that center.
  float mn[3] = {0.0f, 0.0f, 0.0f}, mx[3] = {0.0f, 0.0f, 0.0f};
  bool firstSphere = true;
  for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
    if (!tdKeepObject(*o)) continue;
    float r = fabsf(o->scale()) * td_meshBoundRadius(o->mesh());
    float pos[3] = {o->x(), o->y(), o->z()};
    for (int k = 0; k < 3; ++k) {
      float lo = pos[k] - r, hi = pos[k] + r;
      if (firstSphere || lo < mn[k]) mn[k] = lo;
      if (firstSphere || hi > mx[k]) mx[k] = hi;
    }
    firstSphere = false;
  }
  TD_Vec3 sceneC = {(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f};
  float sceneR = 0.0f;
  for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
    if (!tdKeepObject(*o)) continue;
    float r = fabsf(o->scale()) * td_meshBoundRadius(o->mesh());
    float d = tdLength(tdSub(tdVec3(o->x(), o->y(), o->z()), sceneC)) + r;
    if (d > sceneR) sceneR = d;
  }

  // resolve camera intent (auto-fit latch, orbit pose, focal) against this
  // frame's target dimensions and scene bounds.
  if (!camera.resolve(tw_, th_, sceneC, sceneR)) {
    status_ = TD_ERR_BAD_CAMERA;
    return false;
  }

  status_ = TD_OK;
  return true;
}

void TD_Renderer::drawFrame(const TD_Scene &scene, const TD_Camera &camera,
                            TD_RenderTarget &target) {
  // the camera's resolved rotation already maps world to view (rows = right,
  // up, forward). lighting stays in world space: rotations preserve dot
  // products, so shading with unrotated light and object-rotated normals is
  // identical to view-space shading and cheaper.
  const TD_Mat3 &Rv = camera.rot_;
  const TD_Vec3 eye = camera.eye_;
  const float focal = camera.focalPx_;
  const float pcx = camera.cx_, pcy = camera.cy_;
  const float nearZ = camera.nearSkip_;

  // per-object transform into the shared buffers
  uint32_t vertBase = 0, faceBase = 0;
  uint16_t nKept = 0;
  for (const TD_Object *o = scene.firstObject(); o; o = o->next_) {
    if (!tdKeepObject(*o)) continue;
    const TD_Mesh &msh = o->mesh();
    ObjFrame &of = objs_[nKept];
    of.mesh = &msh;
    of.scale = o->scale();
    of.vertBase = vertBase;
    of.faceBase = faceBase;
    of.baseColor = o->color();
    of.objectIdx = nKept;
    of.material = (uint8_t)o->material();
    of.ink = o->ink();
    of.group = o->inkGroup();
    of.N = tdEulerXYZ(o->rotX(), o->rotY(), o->rotZ());  // normals: rotation only
    of.M = tdMat3ScaleUniform(tdMat3Mul(Rv, of.N), o->scale());
    of.T = tdMat3MulVec(Rv, tdSub(tdVec3(o->x(), o->y(), o->z()), eye));

    const TD_Mat3 &M = of.M;
    const TD_Vec3 T = of.T;
    for (uint16_t i = 0; i < msh.nverts; ++i) {
      float vx = msh.verts[i][0], vy = msh.verts[i][1], vz = msh.verts[i][2];
      // rotate/scale first, translate after the full dot products — pinned
      // evaluation order.
      float wx = M.m[0] * vx + M.m[1] * vy + M.m[2] * vz;
      float wy = M.m[3] * vx + M.m[4] * vy + M.m[5] * vz;
      float wz = M.m[6] * vx + M.m[7] * vy + M.m[8] * vz;
      float px = wx + T.x, py = wy + T.y, pz = wz + T.z;
      uint32_t gi = vertBase + i;
      sz_[gi] = pz;
      if (pz < nearZ) {  // face-level near-skip catches these below
        sx_[gi] = 0.0f;
        sy_[gi] = 0.0f;
        continue;
      }
      sx_[gi] = pcx + focal * px / pz;  // multiply before divide
      sy_[gi] = pcy - focal * py / pz;  // the one y-flip in the library
    }

    // mean view depth over each face's original polygon loop (true faces
    // sort; fan diagonals never skew the key). any loop vertex closer than
    // nearSkip drops the whole face — there is no near-plane clipping. a
    // non-finite mean (NaN vertex) degrades to a skipped face instead of
    // poisoning the sort.
    for (uint16_t f = 0; f < msh.nfaces; ++f) {
      uint16_t loopStart = msh.faces[f][2], loopCount = msh.faces[f][3];
      float zsum = 0.0f;
      bool skip = false;
      for (uint16_t k = 0; k < loopCount; ++k) {
        float z = sz_[vertBase + msh.faceVerts[loopStart + k]];
        if (z < nearZ) {
          skip = true;
          break;
        }
        zsum += z;
      }
      float fz = -1e30f;
      if (!skip) {
        fz = zsum / (float)loopCount;
        if (!isfinite(fz)) {
          fz = -1e30f;
          skip = true;
        }
      }
      if (skip) ++lastSkipped_;
      faceZ_[faceBase + f] = fz;
    }

    vertBase += msh.nverts;
    faceBase += msh.nfaces;
    ++nKept;
  }

  // frontness is tested per triangle: a non-planar quad's fan triangles may
  // disagree, and the face draws if any of them fronts. decal faces join the
  // second draw group, keyed by their own depth pulled nearer per layer; the
  // bias is model-units by intent, so it scales with the object.
  //
  // under TD_OUTLINE only, the per-face facing result is also recorded (for
  // the ink pass's silhouette test) and decal faces are skipped whole —
  // their loop edges are unshared and would ink as permanent boundary
  // rectangles. other modes take no memset and no stores: the disabled path
  // stays byte-identical.
  const bool outlineMode = mode_ == TD_OUTLINE;
  if (outlineMode) memset(faceFront_, TD_FACING_UNKNOWN, (size_t)faceBase);
  uint32_t nRec = 0;
  uint32_t fBase = 0;
  for (uint16_t oi = 0; oi < nKept; ++oi) {
    const ObjFrame &of = objs_[oi];
    const TD_Mesh &msh = *of.mesh;
    const uint32_t vb = of.vertBase;
    for (uint16_t f = 0; f < msh.nfaces; ++f) {
      float fz = faceZ_[fBase + f];
      if (fz <= -1e29f) continue;  // near-skip sentinel (facing stays UNKNOWN)
      if (outlineMode && msh.fattr && msh.fattr[f].host != TD_NO_HOST) continue;
      uint16_t triStart = msh.faces[f][0], triCount = msh.faces[f][1];
      bool anyFront = false;
      for (uint16_t t = triStart; t < triStart + triCount && !anyFront; ++t) {
        uint16_t a = msh.tris[t][0], b = msh.tris[t][1], c = msh.tris[t][2];
        float ax = sx_[vb + a], ay = sy_[vb + a];
        float cross =
            (sx_[vb + b] - ax) * (sy_[vb + c] - ay) - (sy_[vb + b] - ay) * (sx_[vb + c] - ax);
        anyFront = cullSign_ * cross > 0.0f;
      }
      if (outlineMode)
        faceFront_[fBase + f] = anyFront ? TD_FACING_FRONT : TD_FACING_BACK;
      if (!anyFront) continue;

      float zkey = fz;
      uint8_t grp = 0;
      if (msh.fattr && msh.fattr[f].host != TD_NO_HOST) {
        grp = 1;
        // bias by the scale magnitude: the pull must always move decals
        // nearer, and a mirrored (negative-scale) object must not invert
        // its decal stacking order.
        zkey -= (float)(msh.fattr[f].layer + 1) * TD_DECAL_BIAS * fabsf(of.scale);
      }
      rec_[nRec].z = zkey;
      rec_[nRec].obj = oi;
      rec_[nRec].face = f;
      rec_[nRec].grp = grp;
      ++nRec;
    }
    fBase += msh.nfaces;
  }

  // one global painter sort
  if (nRec != 0) qsort(rec_, nRec, sizeof(DrawRec), cmpFarFirst);

  // shade + draw back-to-front
  const TD_Vec3 L = scene.light.direction();
  const float lightIntensity = scene.light.intensity();
  // added lights: the list head, read once per frame beside the other scene
  // latches; the chain is walked per lit corner in add order.
  const TD_Light *extraLights = scene.firstExtraLight();
  const float ambient = scene.ambient();

  // cel banding applies only to LIT gouraud fills; ambient >= 1 collapses
  // every band to 1.0, so the frame degrades to flat-lit fills. latched per
  // drawFrame — the latch never survives from a prior frame or pass.
  celAmbient_ = ambient;
  celFrameOn_ = mode_ == TD_CEL && 1.0f - ambient > 0.0f;

  // TD_OUTLINE keeps the nearest fill depth per pixel across the frame; the
  // ink pass after the fills tests every edge pixel against it.
  depthOn_ = false;
  groupsOn_ = false;
  if (outlineMode) {
    const uint32_t npix = (uint32_t)tw_ * (uint32_t)th_;
    for (uint32_t i = 0; i < npix; ++i) depth_[i] = FLT_MAX;
    // beside each depth, the line group of the fill that wrote it (prepare
    // sized the buffer for any frame with one).
    for (uint16_t oi = 0; oi < nKept; ++oi) groupsOn_ = groupsOn_ || objs_[oi].group != 0;
    if (groupsOn_)
      for (uint32_t i = 0; i < npix; ++i) groups_[i] = 0;
  }

  for (uint32_t i = 0; i < nRec; ++i) {
    const DrawRec &r = rec_[i];
    const ObjFrame &of = objs_[r.obj];
    const TD_Mesh &msh = *of.mesh;
    const uint32_t vb = of.vertBase;
    uint16_t triStart = msh.faces[r.face][0], triCount = msh.faces[r.face][1];
    uint16_t loopStart = msh.faces[r.face][2], loopCount = msh.faces[r.face][3];

    // whole-object material override; lit defers to the mesh's per-face
    // attributes. wireframe edges always draw as lit (base-colored lines on
    // color targets, saturated on mono).
    uint8_t mat = of.material != TD_MATERIAL_LIT
                      ? of.material
                      : (msh.fattr ? msh.fattr[r.face].mat : (uint8_t)TD_MATERIAL_LIT);

    if (outlineMode) {
      // fill pass: the face at the background shade, its view depth written
      // per pixel for the ink pass that follows the loop. WHITE faces fill
      // at 2.0 instead (a face's opt-in to solid white — the eyeball case),
      // keeping WHITE's unconditionally-lit contract; everything else fills
      // at 0.0.
      celSpanOn_ = false;
      const bool whiteFill = mat == TD_MATERIAL_WHITE;
      TD_MaterialCtx ctx;
      ctx.mat = whiteFill ? (uint8_t)TD_MATERIAL_WHITE : (uint8_t)TD_MATERIAL_BLACK;
      ctx.mode = mode_;
      ctx.baseColor = of.baseColor;
      ctx.objectIdx = r.obj;
      ctx.faceIdx = r.face;
      target.setMaterial(ctx);
      depthOn_ = true;
      depthFlat_ = whiteFill ? 2.0f : 0.0f;
      depthGroup_ = of.group;
      for (uint16_t t = triStart; t < triStart + triCount; ++t) {
        // every fan triangle fills — the per-triangle re-cull is skipped so
        // a non-planar face leaves no coverage holes for hidden lines to
        // show through. the interpolant is view depth, the shade is flat.
        uint16_t a = msh.tris[t][0], b = msh.tris[t][1], c = msh.tris[t][2];
        fillTri(target, sx_[vb + a], sy_[vb + a], sz_[vb + a], sx_[vb + b], sy_[vb + b],
                sz_[vb + b], sx_[vb + c], sy_[vb + c], sz_[vb + c]);
      }
      depthOn_ = false;
      continue;
    }

    // quantization is scoped to LIT fills: BLACK/WHITE sentinel shades flow
    // through the untouched span path.
    celSpanOn_ = celFrameOn_ && mat == TD_MATERIAL_LIT;
    TD_MaterialCtx ctx;
    ctx.mat = mode_ == TD_WIREFRAME ? (uint8_t)TD_MATERIAL_LIT : mat;
    ctx.mode = mode_;
    ctx.baseColor = of.baseColor;
    ctx.objectIdx = r.obj;
    ctx.faceIdx = r.face;
    target.setMaterial(ctx);

    if (mode_ != TD_WIREFRAME) {
      for (uint16_t t = triStart; t < triStart + triCount; ++t) {
        uint16_t a = msh.tris[t][0], b = msh.tris[t][1], c = msh.tris[t][2];
        float ax = sx_[vb + a], ay = sy_[vb + a];
        float bx = sx_[vb + b], by = sy_[vb + b];
        float ccx = sx_[vb + c], ccy = sy_[vb + c];
        float cross = (bx - ax) * (ccy - ay) - (by - ay) * (ccx - ax);
        if (cullSign_ * cross <= 0.0f) continue;  // back-facing fan triangle

        // corner shades: 2.0 saturates every dither (solid fill, white);
        // 0.0 never lights (black); lit in shaded and cel modes gets Gouraud
        // clamp(ambient + (1 - ambient) * max(0, n·L) * intensity) with the
        // normal rotated by the object rotation only (world-space shading),
        // plus, per added light, (1 - ambient) * max(0, n·Li) * intensity_i,
        // summed in add order before the clamp.
        // cel keeps the corner shades continuous — quantization happens per
        // span, after interpolation, in celFillSpan.
        float sh[3] = {2.0f, 2.0f, 2.0f};
        if (mat == TD_MATERIAL_BLACK) {
          sh[0] = sh[1] = sh[2] = 0.0f;
        } else if ((mode_ == TD_SHADED || mode_ == TD_CEL) && mat == TD_MATERIAL_LIT) {
          const TD_Mat3 &N = of.N;
          for (int k = 0; k < 3; ++k) {
            const float *cn = msh.cnorms[3 * t + k];
            float nx = N.m[0] * cn[0] + N.m[1] * cn[1] + N.m[2] * cn[2];
            float ny = N.m[3] * cn[0] + N.m[4] * cn[1] + N.m[5] * cn[2];
            float nz = N.m[6] * cn[0] + N.m[7] * cn[1] + N.m[8] * cn[2];
            float diff = nx * L.x + ny * L.y + nz * L.z;
            if (diff < 0.0f) diff = 0.0f;
            float lit = ambient + (1.0f - ambient) * diff * lightIntensity;
            // each added light contributes its own clamped diffuse term, in
            // add order; the loop is empty when no lights were added.
            for (const TD_Light *el = extraLights; el; el = el->next_) {
              float d = nx * el->dir_.x + ny * el->dir_.y + nz * el->dir_.z;
              if (d < 0.0f) d = 0.0f;
              lit += (1.0f - ambient) * d * el->intensity_;
            }
            sh[k] = lit > 1.0f ? 1.0f : lit;
          }
        }
        fillTri(target, ax, ay, sh[0], bx, by, sh[1], ccx, ccy, sh[2]);
      }
    }
    if (!of.ink) continue;  // it filled; it draws no line
    if (mode_ == TD_SOLID || (mode_ == TD_CEL && celInk_ == TD_CEL_INK_BLACK)) {
      // true-edge outlines at exact shade 0.0; the material context is
      // re-latched to black so color targets draw them in the exact palette.
      ctx.mat = TD_MATERIAL_BLACK;
      target.setMaterial(ctx);
      outlineLoop(target, of, loopStart, loopCount, 0.0f);
    } else if (mode_ == TD_CEL && celInk_ == TD_CEL_INK_WHITE) {
      // light ink at exact shade 2.0: reads against dark 1-bit backgrounds;
      // the re-latch gives color targets the exact matWhite palette entry.
      ctx.mat = TD_MATERIAL_WHITE;
      target.setMaterial(ctx);
      outlineLoop(target, of, loopStart, loopCount, 2.0f);
    } else if (mode_ == TD_WIREFRAME) {
      outlineLoop(target, of, loopStart, loopCount, 2.0f);
    }
  }

  // TD_OUTLINE ink pass, after every fill has landed: each drawn face's
  // classified edges, depth-tested per pixel against the nearest fill, so no
  // fill lands on a line after it is drawn and no hidden line surfaces.
  if (outlineMode) {
    for (uint32_t i = 0; i < nRec; ++i) {
      const DrawRec &r = rec_[i];
      const ObjFrame &of = objs_[r.obj];
      if (!of.ink) continue;  // it filled; it inks nothing
      TD_MaterialCtx ctx;
      ctx.mat = TD_MATERIAL_WHITE;
      ctx.mode = mode_;
      ctx.baseColor = of.baseColor;
      ctx.objectIdx = r.obj;
      ctx.faceIdx = r.face;
      target.setMaterial(ctx);
      outlineClassifiedLoop(target, of, r.face);
    }
  }

  lastDrawn_ = nRec;
}

// painter comparator for qsort over DrawRec. draw order:
//   1. grp ascending   — all body faces (0) before all decals (1)
//   2. z descending    — far first within a group
//   3. obj ascending   — tie-break: instances of one mesh tie exactly
//   4. face ascending  — tie-break: coplanar faces within one object
// the trailing tie-breaks leave no two records equal, so the unstable,
// libc-specific qsort yields one deterministic permutation and the sorted
// output is a pure function of the input.
int TD_Renderer::cmpFarFirst(const void *a, const void *b) {
  const DrawRec *ra = (const DrawRec *)a, *rb = (const DrawRec *)b;
  if (ra->grp != rb->grp) return (int)ra->grp - (int)rb->grp;
  if (ra->z > rb->z) return -1;
  if (ra->z < rb->z) return 1;
  if (ra->obj != rb->obj) return (int)ra->obj - (int)rb->obj;
  return (int)ra->face - (int)rb->face;
}

// the cel quantizers. one expression family: td_celBand classifies by
// comparing against the same td_celThresh values celFillSpan solves span
// crossings at, so classification and boundary placement cannot disagree.

float td_celThresh(int k, float ambient, uint8_t bands) {
  return ambient + (1.0f - ambient) * (((float)k + 0.5f) / (float)(bands - 1));
}

int td_celBand(float shade, float ambient, uint8_t bands) {
  // bands <= TD_CEL_MAX_BANDS, so this is at most 7 comparisons.
  int b = 0;
  for (int k = 0; k <= (int)bands - 2; ++k)
    if (shade > td_celThresh(k, ambient, bands)) b = k + 1;  // NaN: all false -> 0
  return b;
}

float td_celLevel(int band, float ambient, uint8_t bands) {
  // endpoint exactness by branch, not arithmetic: ambient + (1 - ambient)
  // is not reliably 1.0f in float.
  if (band <= 0) return ambient;            // bottom band: exactly the shade floor
  if (band >= (int)bands - 1) return 1.0f;  // top band: exactly saturated
  return ambient + (1.0f - ambient) * ((float)band / (float)(bands - 1));
}

// scanline fill sampling pixel centers, half-open on the right/bottom:
// adjacent triangles sharing an edge neither crack nor double-draw. each
// vertex carries its own shade (its view depth under the outline fill);
// edges and spans interpolate it (Gouraud, screen-linear). the row bounds
// are clamped in float space before the int conversion so extreme
// projections cannot overflow the cast; in-range values convert identically
// to the unclamped expression.
void TD_Renderer::fillTri(TD_RenderTarget &t, float x0, float y0, float s0, float x1, float y1,
                          float s1, float x2, float y2, float s2) {
  float tx, ty, ts;
  if (y0 > y1) {
    tx = x0; x0 = x1; x1 = tx;
    ty = y0; y0 = y1; y1 = ty;
    ts = s0; s0 = s1; s1 = ts;
  }
  if (y1 > y2) {
    tx = x1; x1 = x2; x2 = tx;
    ty = y1; y1 = y2; y2 = ty;
    ts = s1; s1 = s2; s2 = ts;
  }
  if (y0 > y1) {
    tx = x0; x0 = x1; x1 = tx;
    ty = y0; y0 = y1; y1 = ty;
    ts = s0; s0 = s1; s1 = ts;
  }
  if (!(y2 - y0 >= 1e-6f)) return;  // degenerate or non-finite

  float fys = ceilf(y0 - 0.5f);  // first row whose center >= y0
  float fye = ceilf(y2 - 0.5f);  // one past the last row whose center < y2
  if (fys < 0.0f) fys = 0.0f;
  if (fys > (float)th_) fys = (float)th_;
  if (fye < 0.0f) fye = 0.0f;
  if (fye > (float)th_) fye = (float)th_;
  int ys = (int)fys;
  int ye = (int)fye - 1;

  for (int y = ys; y <= ye; ++y) {
    float yc = y + 0.5f;
    if (yc < y0 || yc >= y2) continue;  // numeric safety at the seams
    float ta = (yc - y0) / (y2 - y0);   // long edge v0->v2
    float xa = x0 + (x2 - x0) * ta;
    float sa = s0 + (s2 - s0) * ta;
    float xb, sb;
    if (yc < y1) {
      float d = y1 - y0;
      float tb = d > 1e-6f ? (yc - y0) / d : 1.0f;
      xb = x0 + (x1 - x0) * tb;
      sb = s0 + (s1 - s0) * tb;
    } else {
      float d = y2 - y1;
      float tb = d > 1e-6f ? (yc - y1) / d : 0.0f;
      xb = x1 + (x2 - x1) * tb;
      sb = s1 + (s2 - s1) * tb;
    }
    if (xa > xb) {
      float tmp = xa; xa = xb; xb = tmp;
      tmp = sa; sa = sb; sb = tmp;
    }
    if (depthOn_) depthFillSpan(t, y, xa, xb, sa, sb);
    else if (celSpanOn_) celFillSpan(t, y, xa, xb, sa, sb);
    else fillSpan(t, y, xa, xb, sa, sb);
  }
}

// one Gouraud span. the span is clipped to the viewport here, by
// construction — it arrives at the target already in-bounds, so target hot
// loops carry no bounds checks. the seam is crossed exactly once; the target
// evaluates shade(x) = sl + (x + 0.5f - xl) * slope, and a degenerate width
// forces slope 0 so equal endpoints stay exact (flat-lit faces, solid fills,
// and ambient-1.0 silhouettes must saturate).
void TD_Renderer::fillSpan(TD_RenderTarget &t, int y, float xl, float xr, float sl, float sr) {
  if (!(xl <= xr)) return;        // non-finite guard; callers pass xl <= xr
  float fxs = ceilf(xl - 0.5f);   // first pixel whose center >= xl
  float fxe = ceilf(xr - 0.5f);   // one past the last pixel whose center < xr
  if (fxs < 0.0f) fxs = 0.0f;
  if (fxs > (float)tw_) fxs = (float)tw_;
  if (fxe < 0.0f) fxe = 0.0f;
  if (fxe > (float)tw_) fxe = (float)tw_;
  int xs = (int)fxs;
  int xe = (int)fxe - 1;
  if (xe < xs) return;
  float w = xr - xl;
  float slope = w > 1e-6f ? (sr - sl) / w : 0.0f;
  t.plotShadeSpan((int16_t)y, (int16_t)xs, (int16_t)xe, xl, sl, slope);
}

// the outline fill's span: fillSpan's exact pixel selection, each pixel
// keeping the nearer of its stored depth and this span's interpolated view
// depth, then the flat background shade through the ordinary span path.
void TD_Renderer::depthFillSpan(TD_RenderTarget &t, int y, float xl, float xr, float zl,
                                float zr) {
  if (!(xl <= xr)) return;
  float fxs = ceilf(xl - 0.5f);
  float fxe = ceilf(xr - 0.5f);
  if (fxs < 0.0f) fxs = 0.0f;
  if (fxs > (float)tw_) fxs = (float)tw_;
  if (fxe < 0.0f) fxe = 0.0f;
  if (fxe > (float)tw_) fxe = (float)tw_;
  int xs = (int)fxs;
  int xe = (int)fxe - 1;
  if (xe < xs) return;
  float w = xr - xl;
  float slope = w > 1e-6f ? (zr - zl) / w : 0.0f;
  float *row = depth_ + (uint32_t)y * (uint32_t)tw_;
  if (groupsOn_) {
    uint8_t *grow = groups_ + (uint32_t)y * (uint32_t)tw_;
    for (int x = xs; x <= xe; ++x) {
      float z = zl + ((float)x + 0.5f - xl) * slope;
      if (z < row[x]) {
        row[x] = z;
        grow[x] = depthGroup_;
      }
    }
  } else {
    for (int x = xs; x <= xe; ++x) {
      float z = zl + ((float)x + 0.5f - xl) * slope;
      if (z < row[x]) row[x] = z;
    }
  }
  fillSpan(t, y, xl, xr, depthFlat_, depthFlat_);
}

// split one gouraud span at cel band-threshold crossings into flat
// sub-spans, each re-emitted through fillSpan with equal endpoint shades so
// the pinned slope expression yields exactly +0.0f and every target receives
// the exact band level. span shade is screen-linear, so crossings solve
// analytically in x; the monotone clamp makes the sub-spans a crack-free,
// overlap-free partition of [xl, xr]. endpoints arrive unclamped (fillSpan
// clips), so g can overflow on near-skip-grazing geometry — the isfinite
// guard degrades those spans to one flat fill at the left band's level,
// keeping coverage a write.
void TD_Renderer::celFillSpan(TD_RenderTarget &t, int y, float xl, float xr, float sl,
                              float sr) {
  const float a = celAmbient_;
  const uint8_t n = celBands_;
  int bl = td_celBand(sl, a, n), br = td_celBand(sr, a, n);
  if (bl == br) {
    float lv = td_celLevel(bl, a, n);
    fillSpan(t, y, xl, xr, lv, lv);
    return;
  }
  const float g = (xr - xl) / (sr - sl);  // pixels per unit shade; bl != br => sr != sl
  if (!isfinite(g)) {
    float lv = td_celLevel(bl, a, n);
    fillSpan(t, y, xl, xr, lv, lv);
    return;
  }
  const int d = br > bl ? 1 : -1;
  float x0 = xl;
  for (int b = bl; b != br; b += d) {
    int k = d > 0 ? b : b - 1;  // threshold index between bands b and b+d
    float thr = td_celThresh(k, a, n);
    float xc = xl + (thr - sl) * g;
    if (xc < x0) xc = x0;  // monotone partition
    if (xc > xr) xc = xr;
    float lv = td_celLevel(b, a, n);
    fillSpan(t, y, x0, xc, lv, lv);
    x0 = xc;
  }
  float lv = td_celLevel(br, a, n);
  fillSpan(t, y, x0, xr, lv, lv);
}

// Bresenham line through the per-pixel seam. endpoints snap with floorf so
// outlines register with the fill's pixel-center sampling. the float
// endpoints are clipped parametrically to the guard band before any integer
// conversion: near-skip-grazing geometry can project to millions of pixels
// off-screen, which is undefined float->int behavior and an unbounded walk
// if left unclamped. the clip keeps the drawn pixels collinear with the
// true segment (per-axis clamping would bend the visible portion); in-band
// endpoints pass through untouched, and the viewport lies strictly inside
// the band, so nothing visible is ever lost. the per-pixel bounds funnel
// here is the one per-pixel check left in the pipeline (spans are clipped
// by construction).
// Liang-Barsky against the guard band: tightens [t0, t1] along the segment
// from (x0, y0) with direction (dxf, dyf); p is the segment's speed toward
// each band edge, q the start point's signed clearance. false when the
// segment never enters the band.
static bool clipToBand(float x0, float y0, float dxf, float dyf, float bx, float by, float &t0,
                       float &t1) {
  const float p[4] = {-dxf, dxf, -dyf, dyf};
  const float q[4] = {x0 + bx, bx - x0, y0 + by, by - y0};
  for (int k = 0; k < 4; ++k) {
    if (p[k] == 0.0f) {
      if (q[k] < 0.0f) return false;  // parallel to this edge and fully outside
      continue;
    }
    float r = q[k] / p[k];
    if (p[k] < 0.0f) {
      if (r > t0) t0 = r;  // entering the band
    } else {
      if (r < t1) t1 = r;  // leaving the band
    }
  }
  return t0 <= t1;
}

void TD_Renderer::drawLineShade(TD_RenderTarget &t, float x0, float y0, float x1, float y1,
                                float shade) {
  if (!(isfinite(x0) && isfinite(y0) && isfinite(x1) && isfinite(y1))) return;
  const float bx = (float)TD_GUARD_BAND * (float)tw_;
  const float by = (float)TD_GUARD_BAND * (float)th_;
  const float dxf = x1 - x0, dyf = y1 - y0;
  float t0 = 0.0f, t1 = 1.0f;
  if (!clipToBand(x0, y0, dxf, dyf, bx, by, t0, t1)) return;
  if (t1 < 1.0f) {      // clip the far endpoint first: it derives from x0/y0
    x1 = x0 + t1 * dxf;
    y1 = y0 + t1 * dyf;
  }
  if (t0 > 0.0f) {
    x0 = x0 + t0 * dxf;
    y0 = y0 + t0 * dyf;
  }

  int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0);
  int ix1 = (int)floorf(x1), iy1 = (int)floorf(y1);
  int dx = abs(ix1 - ix0), sx = ix0 < ix1 ? 1 : -1;
  int dy = -abs(iy1 - iy0), sy = iy0 < iy1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if ((unsigned)ix0 < (unsigned)tw_ && (unsigned)iy0 < (unsigned)th_)
      t.plotShade((int16_t)ix0, (int16_t)iy0, shade);
    if (ix0 == ix1 && iy0 == iy1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      ix0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      iy0 += sy;
    }
  }
}

// the outline ink pixel test: the edge's depth against the nearest fill at
// the pixel or at one of its four neighbors, within tol. a line and the
// pixel-center fills around it disagree by up to about a pixel, and on a
// fine mesh the pixel center can belong to a face several facets in, so
// the test looks one pixel across the line too: at a silhouette that
// neighbor is uncovered (FLT_MAX, always passes), across a crease it is
// the face on the other side at about the same depth. a hidden line's
// pixels and their neighbors all sit under the occluder, except within one
// pixel of the occluder's own contour. a line in a line group never passes
// on a fill of its own group: only the background or a fill outside the
// group can let it through, so the group draws only its union's contour.
static inline bool inkPass(const float *depth, const uint8_t *groups, uint32_t i, float z,
                           float tol, uint8_t group) {
  if (group != 0 && groups[i] == group) return false;
  return z <= depth[i] + tol;
}
static inline bool inkVisible(const float *depth, const uint8_t *groups, int tw, int th, int x,
                              int y, float z, float tol, uint8_t group) {
  const uint32_t i = (uint32_t)y * (uint32_t)tw + (uint32_t)x;
  if (inkPass(depth, groups, i, z, tol, group)) return true;
  if (x > 0 && inkPass(depth, groups, i - 1, z, tol, group)) return true;
  if (x + 1 < tw && inkPass(depth, groups, i + 1, z, tol, group)) return true;
  if (y > 0 && inkPass(depth, groups, i - (uint32_t)tw, z, tol, group)) return true;
  if (y + 1 < th && inkPass(depth, groups, i + (uint32_t)tw, z, tol, group)) return true;
  if (group == 0) return false;
  // a grouped line can't lean on its own fill, and a fill can reach a pixel
  // past its silhouette line (every fan triangle of a face fills), so where
  // the line steps, the way out to the background may be only diagonal.
  if (x > 0 && y > 0 && inkPass(depth, groups, i - (uint32_t)tw - 1, z, tol, group)) return true;
  if (x + 1 < tw && y > 0 && inkPass(depth, groups, i - (uint32_t)tw + 1, z, tol, group)) return true;
  if (x > 0 && y + 1 < th && inkPass(depth, groups, i + (uint32_t)tw - 1, z, tol, group)) return true;
  if (x + 1 < tw && y + 1 < th && inkPass(depth, groups, i + (uint32_t)tw + 1, z, tol, group)) return true;
  return false;
}

// the outline ink line: drawLineShade's clip and walk with view depth
// interpolated along the segment, each pixel plotted at 2.0 only where
// inkVisible passes it.
void TD_Renderer::drawLineDepth(TD_RenderTarget &t, float x0, float y0, float z0, float x1,
                                float y1, float z1, float tol, uint8_t group) {
  if (!(isfinite(x0) && isfinite(y0) && isfinite(x1) && isfinite(y1))) return;
  const float bx = (float)TD_GUARD_BAND * (float)tw_;
  const float by = (float)TD_GUARD_BAND * (float)th_;
  const float dxf = x1 - x0, dyf = y1 - y0, dzf = z1 - z0;
  float t0 = 0.0f, t1 = 1.0f;
  if (!clipToBand(x0, y0, dxf, dyf, bx, by, t0, t1)) return;
  if (t1 < 1.0f) {
    x1 = x0 + t1 * dxf;
    y1 = y0 + t1 * dyf;
    z1 = z0 + t1 * dzf;
  }
  if (t0 > 0.0f) {
    x0 = x0 + t0 * dxf;
    y0 = y0 + t0 * dyf;
    z0 = z0 + t0 * dzf;
  }

  int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0);
  int ix1 = (int)floorf(x1), iy1 = (int)floorf(y1);
  int dx = abs(ix1 - ix0), sx = ix0 < ix1 ? 1 : -1;
  int dy = -abs(iy1 - iy0), sy = iy0 < iy1 ? 1 : -1;
  int err = dx + dy;
  // the walk advances its major axis once per iteration, so depth steps by
  // the segment's depth span over that many iterations.
  const int steps = dx > -dy ? dx : -dy;
  const float zstep = steps > 0 ? (z1 - z0) / (float)steps : 0.0f;
  float z = z0;
  for (;;) {
    if ((unsigned)ix0 < (unsigned)tw_ && (unsigned)iy0 < (unsigned)th_ &&
        inkVisible(depth_, groups_, tw_, th_, ix0, iy0, z, tol, groupsOn_ ? group : (uint8_t)0))
      t.plotShade((int16_t)ix0, (int16_t)iy0, 2.0f);
    if (ix0 == ix1 && iy0 == iy1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      ix0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      iy0 += sy;
    }
    z += zstep;
  }
}

// outline a face's original polygon loop — true edges, never fan diagonals —
// through the shared vertex buffers at the object's offset.
void TD_Renderer::outlineLoop(TD_RenderTarget &t, const ObjFrame &of, uint16_t loopStart,
                              uint16_t loopCount, float shade) {
  const TD_Mesh &msh = *of.mesh;
  const uint32_t vb = of.vertBase;
  for (uint16_t k = 0; k < loopCount; ++k) {
    uint32_t i = vb + msh.faceVerts[loopStart + k];
    uint32_t j = vb + msh.faceVerts[loopStart + (uint16_t)((k + 1) % loopCount)];
    drawLineShade(t, sx_[i], sy_[i], sx_[j], sy_[j], shade);
  }
}

// TD_OUTLINE's ink pass for one front face, run after every fill of the
// frame has landed: each loop edge draws at 2.0 iff it is a boundary edge,
// a crease edge (baked flags), or a silhouette edge — the face across it is
// not known front-facing this frame (a back neighbor, or UNKNOWN for a
// near-skipped one, which isn't drawn and so makes this edge the boundary of
// drawn geometry). every pixel is depth-tested against the frame's fills
// (inkVisible) with a tolerance scaled by the face's steepest depth slope
// per screen pixel, so a fill's pixel-center sampling around the line never
// hides it while a real occluder in front still does. a crease between two
// front faces draws once per
// face, idempotent at 2.0; both-back and hidden crease edges are never
// visited because back faces never reach the draw loop.
void TD_Renderer::outlineClassifiedLoop(TD_RenderTarget &t, const ObjFrame &of, uint16_t face) {
  const TD_Mesh &msh = *of.mesh;
  const TD_EdgeTable *tab = findEdgeTable(msh);
  if (!tab || tab->nloop != msh.faceVertCount) return;  // prepare builds these
  const uint32_t vb = of.vertBase;
  const uint16_t loopStart = msh.faces[face][2], loopCount = msh.faces[face][3];
  float slope = 0.0f;
  for (uint16_t k = 0; k < loopCount; ++k) {
    uint32_t i = vb + msh.faceVerts[loopStart + k];
    uint32_t j = vb + msh.faceVerts[loopStart + (uint16_t)((k + 1) % loopCount)];
    float ex = sx_[j] - sx_[i], ey = sy_[j] - sy_[i];
    float len = sqrtf(ex * ex + ey * ey);
    if (len < 1.0f) len = 1.0f;  // a sub-pixel edge counts as one pixel wide
    float s = fabsf(sz_[j] - sz_[i]) / len;
    if (s > slope) slope = s;
  }
  // a flat face has no slope; the relative term absorbs interpolation noise
  const float tol = TD_OUTLINE_DEPTH_TOL_PX * slope + fabsf(faceZ_[of.faceBase + face]) * 1e-5f;
  for (uint16_t k = 0; k < loopCount; ++k) {
    const uint16_t slot = (uint16_t)(loopStart + k);
    bool ink = tab->flags[slot] != 0;  // crease or boundary
    if (!ink) {
      const uint16_t opp = tab->opp[slot];
      ink = opp == TD_EDGE_NONE || faceFront_[of.faceBase + opp] != TD_FACING_FRONT;
    }
    if (!ink) continue;
    uint32_t i = vb + msh.faceVerts[slot];
    uint32_t j = vb + msh.faceVerts[loopStart + (uint16_t)((k + 1) % loopCount)];
    drawLineDepth(t, sx_[i], sy_[i], sz_[i], sx_[j], sy_[j], sz_[j], tol, of.group);
  }
}
