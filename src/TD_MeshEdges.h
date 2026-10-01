// SPDX-License-Identifier: MIT
#ifndef TD_MESH_EDGES_H
#define TD_MESH_EDGES_H

#include <stdint.h>

#include "TD_Types.h"

// TD edges format 1 — frozen, like TD_Mesh: generated headers initialize
// this struct positionally and check this handshake.
#define TD_EDGES_FORMAT 1

// a mesh's TD_OUTLINE edge table, baked offline by tools/bake_edges.cpp so it
// can live in flash beside the mesh. the renderer uses it in place of
// building one — no table allocation, no build scratch — while its counts
// match the mesh and its crease matches the scene's outline crease
// (TD_Scene::setOutlineEdges). loop-parallel, one entry per faceVerts[] slot:
//   opp[i]: the face across slot i's directed edge, 0xFFFF when none
//   flags[i]: bit 0 crease, bit 1 boundary
struct TD_MeshEdges {
  const uint16_t *opp;     // faceVertCount entries
  const uint8_t *flags;    // faceVertCount entries
  uint16_t nverts;         // the counts of the mesh it was baked from
  uint16_t nfaces;
  uint16_t faceVertCount;
  float crease;            // the crease angle its flags were baked at, radians
};

#endif  // TD_MESH_EDGES_H
