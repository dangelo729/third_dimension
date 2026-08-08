// SPDX-License-Identifier: MIT
#ifndef TD_INTERNAL_EDGE_TABLE_H
#define TD_INTERNAL_EDGE_TABLE_H

#include <stdint.h>

#include "../TD_Mesh.h"

// internal edge-adjacency table — the classifier data behind TD_OUTLINE's
// feature lines (silhouette / crease / boundary). not part of the public API.
//
// one allocation per table: loop-parallel arrays, one entry per faceVerts[]
// slot, so the draw loop's lookup for face f's loop edge k is the direct
// index faces[f][2] + k — no search. sections are ordered uint16 -> uint8 so
// no padding is ever needed:
//
//   offset 0             uint16_t opp[faceVertCount]    2*faceVertCount bytes
//   o1 = 2*faceVertCount uint8_t  flags[faceVertCount]  1*faceVertCount bytes
//   total = 3 * faceVertCount bytes
//
// opp[i] is the mesh-local index of the face across slot i's directed edge
// (TD_EDGE_NONE when no face lies across it); flags[i] carries the
// classification baked at build time. the table reads flash-resident mesh
// arrays fine but always lives in RAM.

#define TD_EDGE_NONE 0xFFFF

// flags[] bits
#define TD_EDGE_CREASE 0x01    // dihedral sharper than the build's crease angle
#define TD_EDGE_BOUNDARY 0x02  // open-mesh border (or non-manifold extra)

struct TD_EdgeTable {
  // section pointers into the block, valid after build() succeeds. opp is
  // the block base (first section), so release() frees exactly it.
  uint16_t *opp = nullptr;
  uint8_t *flags = nullptr;
  uint16_t nloop = 0;  // entries per section == mesh.faceVertCount at build

  // builds the table for `mesh` against `creaseAngleRad`:
  //   1. weld vertices by exact position (converter meshes duplicate
  //      vertices per face; its adjacency is by welded position — without
  //      this, seams read as permanent boundary lines). -0.0f welds to +0.0f,
  //      matching the converter's value-equality keying.
  //   2. pair directed edges on (min, max) of welded endpoints. unpaired =>
  //      TD_EDGE_NONE + boundary. a 3rd+ face on one edge (non-manifold,
  //      td_validateMesh doesn't forbid it) pairs first-come in slot order,
  //      extras become boundary — degrade, never assert.
  //   3. crease flags from face normals by Newell's method over the true
  //      loop (fan-cross fallback below 1e-12, converter parity — first-fan-
  //      tri cross products disagree on non-planar quads): a paired edge is
  //      a crease iff dot(unit(f1), unit(f2)) < cos(creaseAngleRad), the
  //      exact pairwise test tools/obj2mesh.py reduces to on 2-face edges
  //      (its smooth test is >= cos, so ties shade smooth there and stay
  //      un-inked here).
  //   4. decal faces (fattr host set) contribute no entries; their slots
  //      stay TD_EDGE_NONE / 0 and are never read (OUTLINE skips decals).
  // transient scratch (freed before returning): 16 B/vertex weld keys plus
  // 8 B/loop-slot edge records plus 12 B/face normals. returns false on
  // allocation failure, leaving the empty (safe no-op) state; a zero-count
  // mesh builds successfully to the empty state.
  bool build(const TD_Mesh &mesh, float creaseAngleRad);

  // frees the block and returns to the empty state (safe on an empty table).
  void release();
};

#endif  // TD_INTERNAL_EDGE_TABLE_H
