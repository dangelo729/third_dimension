// SPDX-License-Identifier: MIT
#include "td_mesh_builder.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

// single-block layout. begin() computes byte offsets in this fixed section
// order from the 32-bit request counts and carves all section pointers out
// of one malloc:
//
//   o0 = 0                float       verts[nv][3]      12*nv bytes
//   o1 = o0 + 12*nv       float       cnorms[3*nt][3]   36*nt bytes
//   o2 = o1 + 36*nt       uint16_t    tris[nt][3]        6*nt bytes
//   o3 = o2 + 6*nt        uint16_t    faces[nf][4]       8*nf bytes
//   o4 = o3 + 8*nf        uint16_t    faceVerts[nloop]   2*nloop bytes
//   o5 = o4 + 2*nloop     TD_FaceAttr fattr[nf]          4*nf bytes (withAttr only)
//   total = 12*nv + 42*nt + 8*nf + 2*nloop  (+ 4*nf when withAttr)
//
// no-padding invariant: the float sections come first and their sizes
// (12*nv, 36*nt) are multiples of 4, so the uint16 sections start 4-aligned;
// every uint16 section size is even, so each later section — including the
// 2-aligned TD_FaceAttr array — lands on a valid boundary. no offset ever
// needs rounding, which is why the mesh RAM formula is exact.
//
// cursor discipline: generators fill strictly forward; each cursor advances
// monotonically from 0 to exactly its begin() count and nothing is ever
// rewritten:
//   - addVert writes verts[vAt] and returns vAt++, so a generator captures
//     vertex indices as it emits them;
//   - addTri writes tris[tAt] and the corner normals
//     cnorms[3*tAt .. 3*tAt+2], then advances tAt. cnorms is addressed as
//     3*tri + corner with 32-bit arithmetic: 3*nt can exceed uint16 even
//     though nt itself cannot;
//   - beginFace latches curTriStart_ = tAt and curLoopStart_ = lAt; every
//     addTri/loopVert until the matching endFace belongs to that face;
//   - loopVert appends faceVerts[lAt++];
//   - endFace writes faces[fAt++] = { curTriStart_, tAt - curTriStart_,
//     curLoopStart_, lAt - curLoopStart_ } — so face ranges tile [0, nt)
//     and [0, nloop) contiguously by construction, exactly the property
//     td_validateMesh checks;
//   - finish requires every cursor to equal its begin() count (asserted in
//     debug builds), then transfers block ownership into the TD_Mesh view.

bool TD_MeshBuilder::begin(uint32_t nv, uint32_t nt, uint32_t nf, uint32_t nloop,
                           bool withAttr) {
  abort();  // drop any abandoned block so re-begin never leaks
  if (nv > 0xFFFFu || nt > 0xFFFFu || nf > 0xFFFFu || nloop > 0xFFFFu) return false;
  // counts are <= 0xFFFF here, so every product below fits comfortably in
  // uint32_t (36*0xFFFF < 2^22) and the total in a size_t.
  uint32_t o1 = 12u * nv;           // cnorms
  uint32_t o2 = o1 + 36u * nt;      // tris
  uint32_t o3 = o2 + 6u * nt;       // faces
  uint32_t o4 = o3 + 8u * nf;       // faceVerts
  uint32_t o5 = o4 + 2u * nloop;    // fattr (when requested)
  uint32_t total = withAttr ? o5 + 4u * nf : o5;
  uint8_t *base = (uint8_t *)malloc((size_t)total);
  if (!base) return false;
  block_ = base;
  bytes_ = total;
  verts = (float(*)[3])(base);
  cnorms = (float(*)[3])(base + o1);
  tris = (uint16_t(*)[3])(base + o2);
  faces = (uint16_t(*)[4])(base + o3);
  faceVerts = (uint16_t *)(base + o4);
  fattr = withAttr ? (TD_FaceAttr *)(base + o5) : nullptr;
  nv_ = nv;
  nt_ = nt;
  nf_ = nf;
  nloop_ = nloop;
  vAt = tAt = fAt = lAt = 0;
  curTriStart_ = curLoopStart_ = 0;
  return true;
}

uint16_t TD_MeshBuilder::addVert(float x, float y, float z) {
  verts[vAt][0] = x;
  verts[vAt][1] = y;
  verts[vAt][2] = z;
  return vAt++;
}

void TD_MeshBuilder::addTri(uint16_t a, uint16_t b, uint16_t c, const float na[3],
                            const float nb[3], const float nc[3]) {
  tris[tAt][0] = a;
  tris[tAt][1] = b;
  tris[tAt][2] = c;
  float(*corner)[3] = &cnorms[(uint32_t)3 * tAt];
  for (int k = 0; k < 3; ++k) {
    corner[0][k] = na[k];
    corner[1][k] = nb[k];
    corner[2][k] = nc[k];
  }
  ++tAt;
}

void TD_MeshBuilder::beginFace() {
  curTriStart_ = tAt;
  curLoopStart_ = lAt;
}

void TD_MeshBuilder::loopVert(uint16_t v) { faceVerts[lAt++] = v; }

void TD_MeshBuilder::endFace() {
  faces[fAt][0] = curTriStart_;
  faces[fAt][1] = (uint16_t)(tAt - curTriStart_);
  faces[fAt][2] = curLoopStart_;
  faces[fAt][3] = (uint16_t)(lAt - curLoopStart_);
  ++fAt;
}

bool TD_MeshBuilder::finish(TD_Mesh &out, float boundRadius) {
  if (!block_) return false;
  assert(vAt == nv_ && tAt == nt_ && fAt == nf_ && lAt == nloop_);
  out.verts = verts;
  out.tris = tris;
  out.cnorms = cnorms;
  out.faces = faces;
  out.faceVerts = faceVerts;
  out.fattr = fattr;
  out.nverts = (uint16_t)nv_;
  out.ntris = (uint16_t)nt_;
  out.nfaces = (uint16_t)nf_;
  out.faceVertCount = (uint16_t)nloop_;
  out.boundRadius = boundRadius;
  // ownership moves to the caller: the builder forgets the block and resets
  // to the empty state so a later abort()/begin() cannot free the mesh.
  block_ = nullptr;
  bytes_ = 0;
  nv_ = nt_ = nf_ = nloop_ = 0;
  verts = nullptr;
  cnorms = nullptr;
  tris = nullptr;
  faces = nullptr;
  faceVerts = nullptr;
  fattr = nullptr;
  vAt = tAt = fAt = lAt = 0;
  curTriStart_ = curLoopStart_ = 0;
  return true;
}

void TD_MeshBuilder::freeMesh(TD_Mesh &mesh) {
  // mesh.verts is the block base by construction (verts is the first
  // section, at offset 0), so the whole mesh is one free(). zeroing the
  // view afterwards makes repeated teardown and never-built meshes harmless.
  if (mesh.verts) free((void *)mesh.verts);
  mesh = TD_Mesh{};
}

void TD_MeshBuilder::abort() {
  free(block_);
  block_ = nullptr;
  bytes_ = 0;
  nv_ = nt_ = nf_ = nloop_ = 0;
  verts = nullptr;
  cnorms = nullptr;
  tris = nullptr;
  faces = nullptr;
  faceVerts = nullptr;
  fattr = nullptr;
  vAt = tAt = fAt = lAt = 0;
  curTriStart_ = curLoopStart_ = 0;
}
