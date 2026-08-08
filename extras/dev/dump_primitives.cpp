// SPDX-License-Identifier: MIT
// dump_primitives — dev-only host dump of every TD_Primitives generator as
// JSON, for structural diffing against the JS ports in tools/editor.html
// (open the editor with #dev and load this dump; indices must match exactly,
// floats to 1e-6). not a test: it has no CHECKs and lives outside test/host/
// so neither the CI glob nor the Makefile's test_*.cpp pattern ever sees its
// main(). build and run from this directory with:
//
//   clang++ -std=c++17 -O1 -ffp-contract=off -I../../src dump_primitives.cpp ../../src/TD_Primitives.cpp ../../src/internal/td_mesh_builder.cpp -o dump_primitives && ./dump_primitives > primitives_dump.json
//
// each case records the constructor arguments as passed (clamping is the
// generator's own behavior, reproduced by the JS ports) and the resulting
// mesh arrays verbatim. floats print with %.9g so every float32 value
// round-trips exactly.

#include <stdio.h>

#include "TD_Primitives.h"

static void printFloat(float v) { printf("%.9g", (double)v); }

static void dumpMesh(const TD_Mesh &m) {
  printf("\"mesh\":{");
  printf("\"nverts\":%u,\"ntris\":%u,\"nfaces\":%u,\"faceVertCount\":%u,\"boundRadius\":",
         (unsigned)m.nverts, (unsigned)m.ntris, (unsigned)m.nfaces,
         (unsigned)m.faceVertCount);
  printFloat(m.boundRadius);
  printf(",\n\"verts\":[");
  for (unsigned i = 0; i < m.nverts; ++i) {
    printf("%s[", i ? "," : "");
    for (int k = 0; k < 3; ++k) {
      if (k) printf(",");
      printFloat(m.verts[i][k]);
    }
    printf("]");
  }
  printf("],\n\"tris\":[");
  for (unsigned i = 0; i < m.ntris; ++i)
    printf("%s[%u,%u,%u]", i ? "," : "", (unsigned)m.tris[i][0], (unsigned)m.tris[i][1],
           (unsigned)m.tris[i][2]);
  printf("],\n\"cnorms\":[");
  for (unsigned i = 0; i < 3u * m.ntris; ++i) {
    printf("%s[", i ? "," : "");
    for (int k = 0; k < 3; ++k) {
      if (k) printf(",");
      printFloat(m.cnorms[i][k]);
    }
    printf("]");
  }
  printf("],\n\"faces\":[");
  for (unsigned i = 0; i < m.nfaces; ++i)
    printf("%s[%u,%u,%u,%u]", i ? "," : "", (unsigned)m.faces[i][0],
           (unsigned)m.faces[i][1], (unsigned)m.faces[i][2], (unsigned)m.faces[i][3]);
  printf("],\n\"faceVerts\":[");
  for (unsigned i = 0; i < m.faceVertCount; ++i)
    printf("%s%u", i ? "," : "", (unsigned)m.faceVerts[i]);
  printf("]}");
}

// one dump case: name, the primitive type, the constructor arguments as a
// JSON params object (mirroring the editor's project-file params), the mesh.
template <typename MeshT, typename... Args>
static void dumpCase(bool &first, const char *name, const char *type, const char *paramsJson,
                     Args... args) {
  MeshT mesh(args...);
  printf("%s\n{\"name\":\"%s\",\"type\":\"%s\",\"params\":%s,\n", first ? "" : ",", name,
         type, paramsJson);
  dumpMesh(mesh);
  printf("}");
  first = false;
}

int main() {
  bool first = true;
  printf("{\"format\":\"td-primitive-dump-1\",\"cases\":[");

  // defaults — one case per primitive at its header-default arguments.
  dumpCase<TD_BoxMesh>(first, "box_default", "box", "{\"w\":1,\"h\":1,\"d\":1}", 1.0f, 1.0f,
                       1.0f);
  dumpCase<TD_SphereMesh>(first, "sphere_default", "sphere",
                          "{\"slices\":16,\"stacks\":8,\"radius\":0.5}", (uint16_t)16,
                          (uint16_t)8, 0.5f);
  dumpCase<TD_EllipsoidMesh>(
      first, "ellipsoid_default", "ellipsoid",
      "{\"slices\":16,\"stacks\":8,\"rx\":0.5,\"ry\":0.25,\"rz\":0.5}", (uint16_t)16,
      (uint16_t)8, 0.5f, 0.25f, 0.5f);
  dumpCase<TD_CylinderMesh>(first, "cylinder_default", "cylinder",
                            "{\"sides\":16,\"height\":1,\"radius\":0.5}", (uint16_t)16, 1.0f,
                            0.5f);
  dumpCase<TD_ConeMesh>(first, "cone_default", "cone",
                        "{\"sides\":16,\"height\":1,\"radius\":0.5}", (uint16_t)16, 1.0f,
                        0.5f);
  dumpCase<TD_PrismMesh>(first, "prism_default", "prism",
                         "{\"sides\":6,\"height\":1,\"radius\":0.5}", (uint16_t)6, 1.0f,
                         0.5f);
  dumpCase<TD_TorusMesh>(first, "torus_default", "torus",
                         "{\"segU\":16,\"segV\":8,\"tubeRadius\":0.15,\"ringRadius\":0.35}",
                         (uint16_t)16, (uint16_t)8, 0.15f, 0.35f);
  dumpCase<TD_PlaneMesh>(first, "plane_default", "plane",
                         "{\"cols\":1,\"rows\":1,\"width\":1,\"depth\":1}", (uint16_t)1,
                         (uint16_t)1, 1.0f, 1.0f);

  // sweep — non-default dimensions and tessellations, including non-uniform
  // sizes and the odd/low counts that exercise every wrap and fan branch.
  dumpCase<TD_BoxMesh>(first, "box_sweep", "box", "{\"w\":2,\"h\":0.5,\"d\":1.25}", 2.0f,
                       0.5f, 1.25f);
  dumpCase<TD_SphereMesh>(first, "sphere_sweep", "sphere",
                          "{\"slices\":12,\"stacks\":6,\"radius\":0.75}", (uint16_t)12,
                          (uint16_t)6, 0.75f);
  dumpCase<TD_SphereMesh>(first, "sphere_min", "sphere",
                          "{\"slices\":3,\"stacks\":2,\"radius\":1}", (uint16_t)3,
                          (uint16_t)2, 1.0f);
  dumpCase<TD_EllipsoidMesh>(
      first, "ellipsoid_sweep", "ellipsoid",
      "{\"slices\":12,\"stacks\":6,\"rx\":0.4,\"ry\":0.6,\"rz\":0.2}", (uint16_t)12,
      (uint16_t)6, 0.4f, 0.6f, 0.2f);
  // rx = ry = rz must reproduce the sphere vertex-for-vertex (header claim);
  // the editor cross-checks its scaled-sphere normals against this case.
  dumpCase<TD_EllipsoidMesh>(
      first, "ellipsoid_round", "ellipsoid",
      "{\"slices\":12,\"stacks\":6,\"rx\":0.75,\"ry\":0.75,\"rz\":0.75}", (uint16_t)12,
      (uint16_t)6, 0.75f, 0.75f, 0.75f);
  dumpCase<TD_CylinderMesh>(first, "cylinder_sweep", "cylinder",
                            "{\"sides\":5,\"height\":2,\"radius\":0.3}", (uint16_t)5, 2.0f,
                            0.3f);
  dumpCase<TD_ConeMesh>(first, "cone_sweep", "cone",
                        "{\"sides\":7,\"height\":1.5,\"radius\":0.4}", (uint16_t)7, 1.5f,
                        0.4f);
  dumpCase<TD_PrismMesh>(first, "prism_sweep", "prism",
                         "{\"sides\":3,\"height\":0.8,\"radius\":0.6}", (uint16_t)3, 0.8f,
                         0.6f);
  dumpCase<TD_TorusMesh>(first, "torus_sweep", "torus",
                         "{\"segU\":10,\"segV\":6,\"tubeRadius\":0.1,\"ringRadius\":0.5}",
                         (uint16_t)10, (uint16_t)6, 0.1f, 0.5f);
  dumpCase<TD_PlaneMesh>(first, "plane_sweep", "plane",
                         "{\"cols\":4,\"rows\":3,\"width\":2,\"depth\":1.5}", (uint16_t)4,
                         (uint16_t)3, 2.0f, 1.5f);

  // clamp behavior — tessellation below the minimum clamps up instead of
  // failing; the JS ports must clamp identically.
  dumpCase<TD_SphereMesh>(first, "sphere_clamp", "sphere",
                          "{\"slices\":1,\"stacks\":1,\"radius\":0.5}", (uint16_t)1,
                          (uint16_t)1, 0.5f);
  dumpCase<TD_EllipsoidMesh>(
      first, "ellipsoid_clamp", "ellipsoid",
      "{\"slices\":2,\"stacks\":1,\"rx\":0.5,\"ry\":0.25,\"rz\":0.5}", (uint16_t)2,
      (uint16_t)1, 0.5f, 0.25f, 0.5f);
  dumpCase<TD_CylinderMesh>(first, "cylinder_clamp", "cylinder",
                            "{\"sides\":2,\"height\":1,\"radius\":0.5}", (uint16_t)2, 1.0f,
                            0.5f);
  dumpCase<TD_ConeMesh>(first, "cone_clamp", "cone",
                        "{\"sides\":1,\"height\":1,\"radius\":0.5}", (uint16_t)1, 1.0f,
                        0.5f);
  dumpCase<TD_PrismMesh>(first, "prism_clamp", "prism",
                         "{\"sides\":2,\"height\":1,\"radius\":0.5}", (uint16_t)2, 1.0f,
                         0.5f);
  dumpCase<TD_TorusMesh>(first, "torus_clamp", "torus",
                         "{\"segU\":2,\"segV\":2,\"tubeRadius\":0.15,\"ringRadius\":0.35}",
                         (uint16_t)2, (uint16_t)2, 0.15f, 0.35f);
  dumpCase<TD_PlaneMesh>(first, "plane_clamp", "plane",
                         "{\"cols\":0,\"rows\":0,\"width\":1,\"depth\":1}", (uint16_t)0,
                         (uint16_t)0, 1.0f, 1.0f);

  printf("\n]}\n");
  return 0;
}
