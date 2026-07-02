// SPDX-License-Identifier: MIT
#ifndef TD_MESH_H
#define TD_MESH_H

#include <stdint.h>

#include "TD_Types.h"

// TD mesh format 1 — frozen. a non-owning view over geometry arrays that can
// live in flash (converter-generated headers) or RAM (procedural primitives).
// generated headers initialize this struct positionally and carry a
// TD_MESH_FORMAT handshake; the layout can never be reordered or retyped —
// a future change is a new struct and a new format number.
//
// structure:
//   - tris: fan triangles in face-major order (fans over convex source faces)
//   - cnorms: 3 entries per triangle — per-corner unit normals in model
//     space, crease-smoothed by the converter/generators (Gouraud shading)
//   - faces/faceVerts: the original polygon loops, so depth sorting and
//     SOLID/WIREFRAME outlines work on true faces, never fan diagonals
//   - winding: CCW viewed from outside (positive signed volume when closed)
//   - all indices 0-based uint16_t; hard cap 65535 verts/tris/faces/loops
struct TD_Mesh {
  const float (*verts)[3];      // model-space vertex positions
  const uint16_t (*tris)[3];    // fan triangles, face-major
  const float (*cnorms)[3];     // 3 per triangle: corner normals (Gouraud)
  const uint16_t (*faces)[4];   // { triStart, triCount, loopStart, loopCount }
  const uint16_t *faceVerts;    // flattened original polygon loops
  const TD_FaceAttr *fattr;     // optional per-face attrs; nullptr = all
                                //   { TD_MATERIAL_LIT, 0, TD_NO_HOST }
  uint16_t nverts;
  uint16_t ntris;
  uint16_t nfaces;
  uint16_t faceVertCount;       // length of faceVerts[] (enables validation)
  float boundRadius;            // model-space bounding-sphere radius; the
                                //   converter emits 1.0 after normalization,
                                //   generators compute exactly; 0.0 is read
                                //   as "assume 1.0" by consumers
};

// one-time structural validation for hand-built meshes: index ranges, face
// ranges tiling [0, ntris) and [0, faceVertCount) contiguously, loopCount
// >= 3, fattr host indices in range. the render hot path trusts the mesh
// completely — call this once after building, not per frame.
bool td_validateMesh(const TD_Mesh &mesh);

// boundRadius with the zero-means-one guard applied.
static inline float td_meshBoundRadius(const TD_Mesh &mesh) {
  return mesh.boundRadius > 0.0f ? mesh.boundRadius : 1.0f;
}

#endif  // TD_MESH_H
