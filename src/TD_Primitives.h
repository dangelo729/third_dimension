// SPDX-License-Identifier: MIT
#ifndef TD_PRIMITIVES_H
#define TD_PRIMITIVES_H

#include <stdint.h>

#include "TD_Mesh.h"

// procedural primitives as constructor-classes: declare one at global scope
// like any Arduino object and hand it to a TD_Object. Each constructor
// builds the full TD_Mesh arrays (one heap block) with the same structure
// the OBJ converter emits — face-major fan triangles, quad/n-gon polygon
// loops (true-edge outlines, no fan diagonals), analytic per-corner normals
// matching the converter's 60-degree crease semantics, CCW-outward winding —
// so primitives feed the identical render path as converted models.
//
// size convention: explicit dimensions (a primitive's scale is its
// parameters; composition at natural relative sizes stays possible), with
// defaults chosen so every primitive fits the unit cube [-0.5, +0.5]^3.
// boundRadius is computed exactly per shape, so camera auto-fit works the
// same for generated and converted meshes.
//
// failure contract (no exceptions): on out-of-memory, non-positive
// dimensions, or uint16 index overflow the mesh is left empty (zero counts)
// — rendering it is a safe no-op — and valid() returns false. tessellation
// counts clamp to their minimums instead (always renders something
// recognizable). lifetime: the mesh object must outlive any TD_Object /
// scene referencing it, exactly like a flash mesh header.
//
// memory (single block): 12*nv + 42*nt + 8*nf + 2*nloop bytes. defaults:
// box 696 B, sphere(16,8) 13.4 KB, torus(16,8) 14.3 KB; corner normals are
// ~64% of every budget.

class TD_BoxMesh : public TD_Mesh {
 public:
  explicit TD_BoxMesh(float w = 1.0f, float h = 1.0f, float d = 1.0f);
  ~TD_BoxMesh();
  TD_BoxMesh(const TD_BoxMesh &) = delete;
  TD_BoxMesh &operator=(const TD_BoxMesh &) = delete;
  bool valid() const;
};

// UV sphere: slices >= 3 around the equator, stacks >= 2 pole to pole.
// fully smooth (analytic normals n = v/|v|); poles are single vertices.
class TD_SphereMesh : public TD_Mesh {
 public:
  explicit TD_SphereMesh(uint16_t slices = 16, uint16_t stacks = 8, float radius = 0.5f);
  ~TD_SphereMesh();
  TD_SphereMesh(const TD_SphereMesh &) = delete;
  TD_SphereMesh &operator=(const TD_SphereMesh &) = delete;
  bool valid() const;
};

// smooth-shaded tube wall + hard-creased n-gon caps (cap rims outline as
// true polygons and depth-sort as single faces).
class TD_CylinderMesh : public TD_Mesh {
 public:
  explicit TD_CylinderMesh(uint16_t sides = 16, float height = 1.0f, float radius = 0.5f);
  ~TD_CylinderMesh();
  TD_CylinderMesh(const TD_CylinderMesh &) = delete;
  TD_CylinderMesh &operator=(const TD_CylinderMesh &) = delete;
  bool valid() const;
};

// smooth flanks with per-face apex normals (no apex highlight singularity),
// hard-creased n-gon base.
class TD_ConeMesh : public TD_Mesh {
 public:
  explicit TD_ConeMesh(uint16_t sides = 16, float height = 1.0f, float radius = 0.5f);
  ~TD_ConeMesh();
  TD_ConeMesh(const TD_ConeMesh &) = delete;
  TD_ConeMesh &operator=(const TD_ConeMesh &) = delete;
  bool valid() const;
};

// cylinder geometry with flat-shaded sides (per-face normals): the faceted
// look. radius is the circumradius.
class TD_PrismMesh : public TD_Mesh {
 public:
  explicit TD_PrismMesh(uint16_t sides = 6, float height = 1.0f, float radius = 0.5f);
  ~TD_PrismMesh();
  TD_PrismMesh(const TD_PrismMesh &) = delete;
  TD_PrismMesh &operator=(const TD_PrismMesh &) = delete;
  bool valid() const;
};

// fully smooth torus; requires ringRadius > tubeRadius > 0 for a
// non-self-intersecting donut (else invalid).
class TD_TorusMesh : public TD_Mesh {
 public:
  explicit TD_TorusMesh(uint16_t segU = 16, uint16_t segV = 8, float tubeRadius = 0.15f,
                        float ringRadius = 0.35f);
  ~TD_TorusMesh();
  TD_TorusMesh(const TD_TorusMesh &) = delete;
  TD_TorusMesh &operator=(const TD_TorusMesh &) = delete;
  bool valid() const;
};

// single-sided XZ grid facing +Y (back-face culled from below — this is by
// design; it exists for Gouraud gradients and wireframe grids, and its
// subdivision is also the mitigation for painter mis-sorts of large floors).
// open mesh: exempt from the closed-mesh volume/manifold invariants.
class TD_PlaneMesh : public TD_Mesh {
 public:
  explicit TD_PlaneMesh(uint16_t cols = 1, uint16_t rows = 1, float width = 1.0f,
                        float depth = 1.0f);
  ~TD_PlaneMesh();
  TD_PlaneMesh(const TD_PlaneMesh &) = delete;
  TD_PlaneMesh &operator=(const TD_PlaneMesh &) = delete;
  bool valid() const;
};

#endif  // TD_PRIMITIVES_H
