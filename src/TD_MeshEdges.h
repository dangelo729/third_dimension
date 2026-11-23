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
//   opp[i]: the face across slot i's directed edge, TD_EDGE_NONE when none
//   flags[i]: TD_EDGE_CREASE and TD_EDGE_BOUNDARY bits
#define TD_EDGE_NONE 0xFFFF
#define TD_EDGE_CREASE 0x01    // dihedral sharper than the crease angle
#define TD_EDGE_BOUNDARY 0x02  // open-mesh border (or non-manifold extra)

struct TD_MeshEdges {
  const uint16_t *opp;     // faceVertCount entries
  const uint8_t *flags;    // faceVertCount entries
  uint16_t nverts;         // the counts of the mesh it was baked from
  uint16_t nfaces;
  uint16_t faceVertCount;
  float crease;            // the crease angle its flags were baked at, radians
};

#endif  // TD_MESH_EDGES_H
