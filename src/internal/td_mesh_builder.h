// SPDX-License-Identifier: MIT
#ifndef TD_INTERNAL_MESH_BUILDER_H
#define TD_INTERNAL_MESH_BUILDER_H

#include <stdint.h>

#include "../TD_Mesh.h"

// internal single-malloc mesh block builder — the machinery behind the
// TD_Primitives constructor-classes. not part of the public API.
//
// one allocation per mesh: sections are ordered float -> uint16 so section
// sizes are always multiples of the next section's alignment and no padding
// is ever needed:
//
//   offset 0            float verts [nv][3]     12*nv bytes
//   o1 = 12*nv          float cnorms[3*nt][3]   36*nt bytes
//   o2 = o1 + 36*nt     u16   tris  [nt][3]      6*nt bytes
//   o3 = o2 + 6*nt      u16   faces [nf][4]      8*nf bytes
//   o4 = o3 + 8*nf      u16   faceVerts[nloop]   2*nloop bytes
//   o5 = o4 + 2*nloop   TD_FaceAttr fattr[nf]    4*nf bytes (only if requested)
//
// one free tears it down; there are no partial-failure states to unwind.
struct TD_MeshBuilder {
  // computes the layout from 32-bit counts and allocates the block.
  // returns false (leaving the builder empty) on allocation failure or if
  // any count exceeds the uint16 mesh-format limits.
  bool begin(uint32_t nv, uint32_t nt, uint32_t nf, uint32_t nloop, bool withAttr);

  // section pointers into the block, valid after begin() succeeds.
  float (*verts)[3] = nullptr;
  float (*cnorms)[3] = nullptr;
  uint16_t (*tris)[3] = nullptr;
  uint16_t (*faces)[4] = nullptr;
  uint16_t *faceVerts = nullptr;
  TD_FaceAttr *fattr = nullptr;  // nullptr unless withAttr

  // append cursors (generators fill strictly forward).
  uint16_t vAt = 0, tAt = 0, fAt = 0, lAt = 0;

  uint16_t addVert(float x, float y, float z);
  // writes tris[tAt] and its three corner normals cnorms[3*tAt .. 3*tAt+2].
  void addTri(uint16_t a, uint16_t b, uint16_t c, const float na[3], const float nb[3],
              const float nc[3]);
  void beginFace();          // records triStart/loopStart for the face
  void loopVert(uint16_t v); // appends to the face's polygon loop
  void endFace();            // writes faces[fAt] = {triStart, triCount, loopStart, loopCount}

  // fills `out` with the block's array pointers and final counts and
  // transfers block ownership to the caller (freed with freeMesh).
  // debug builds assert every cursor equals its begin() count.
  bool finish(TD_Mesh &out, float boundRadius);

  // frees a block previously handed out by finish() (mesh.verts is the
  // block base by construction).
  static void freeMesh(TD_Mesh &mesh);

  // abandons an unfinished build.
  void abort();

 private:
  void *block_ = nullptr;
  uint32_t bytes_ = 0;
  uint32_t nv_ = 0, nt_ = 0, nf_ = 0, nloop_ = 0;
  uint16_t curTriStart_ = 0, curLoopStart_ = 0;
};

#endif  // TD_INTERNAL_MESH_BUILDER_H
