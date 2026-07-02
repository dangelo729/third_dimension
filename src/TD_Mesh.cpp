// SPDX-License-Identifier: MIT

#include "TD_Mesh.h"

// one-time structural validation for hand-built meshes. performs, in order:
//   - array presence: verts/tris/cnorms/faces/faceVerts non-null wherever
//     the corresponding count is non-zero (an all-zero-counts mesh — the
//     empty/failed-primitive state — is valid)
//   - index ranges: every tris[i][k] < nverts and every faceVerts[i] < nverts
//   - face triangle ranges { triStart, triCount } tile [0, ntris) exactly —
//     contiguous, in face order, no gaps or overlaps, counts summing to ntris
//   - face loop ranges { loopStart, loopCount } tile [0, faceVertCount)
//     exactly, under the same contiguity rule
//   - loopCount >= 3 for every face (a polygon loop is at least a triangle)
//   - fattr host range: if fattr is non-null, every fattr[i].host < nfaces
//     or == TD_NO_HOST
// returns false on the first violation. the render hot path trusts the mesh
// completely — this runs once after building, never per frame.
bool td_validateMesh(const TD_Mesh &mesh) {
  if (mesh.nverts != 0 && mesh.verts == nullptr) return false;
  if (mesh.ntris != 0 && (mesh.tris == nullptr || mesh.cnorms == nullptr)) return false;
  if (mesh.nfaces != 0 && mesh.faces == nullptr) return false;
  if (mesh.faceVertCount != 0 && mesh.faceVerts == nullptr) return false;

  for (uint32_t t = 0; t < mesh.ntris; ++t) {
    for (int k = 0; k < 3; ++k) {
      if (mesh.tris[t][k] >= mesh.nverts) return false;
    }
  }
  for (uint32_t i = 0; i < mesh.faceVertCount; ++i) {
    if (mesh.faceVerts[i] >= mesh.nverts) return false;
  }

  // running cursors: each face must start exactly where the previous one
  // ended, and both cursors must land exactly on the totals. uint32_t
  // accumulators cannot overflow (65535 faces x 65535-capped counts).
  uint32_t triAt = 0;
  uint32_t loopAt = 0;
  for (uint32_t f = 0; f < mesh.nfaces; ++f) {
    const uint16_t *fc = mesh.faces[f];
    if (fc[0] != triAt) return false;
    if (fc[2] != loopAt) return false;
    if (fc[3] < 3) return false;
    triAt += fc[1];
    loopAt += fc[3];
  }
  if (triAt != mesh.ntris) return false;
  if (loopAt != mesh.faceVertCount) return false;

  if (mesh.fattr != nullptr) {
    for (uint32_t f = 0; f < mesh.nfaces; ++f) {
      if (mesh.fattr[f].host != TD_NO_HOST && mesh.fattr[f].host >= mesh.nfaces) return false;
    }
  }
  return true;
}
