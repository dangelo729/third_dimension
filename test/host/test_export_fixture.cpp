// SPDX-License-Identifier: MIT
// export-fixture suite: TD_Model_ExportFixture.h is a checked-in header
// baked by tools/editor.html (a box body, a non-uniformly scaled sphere
// head — the inverse-transpose normal path — and a rotated cone hat) and
// serves as the permanent regression fixture for the editor's exporter:
// it must keep compiling against TD mesh format 1, keep passing
// td_validateMesh, keep the converter-parity normalization invariants
// (boundRadius 1.0, farthest vertex at distance 1, positive signed volume,
// unit corner normals), and keep rendering with a nonzero draw count.

#include <cmath>

#include "TD_MonoTarget.h"
#include "TD_Scene.h"

#include "TD_Model_ExportFixture.h"
#include "test_common.h"

void runExportFixtureTests() {
  const TD_Mesh &m = export_fixture_mesh;

  // structural validity plus the exact counts the scene bakes to (a count
  // drift means the editor's generators or merge changed behavior).
  CHECK(td_validateMesh(m), "exported fixture failed td_validateMesh");
  CHECK(m.nverts == 59, "fixture nverts %u, expected 59", (unsigned)m.nverts);
  CHECK(m.ntris == 106, "fixture ntris %u, expected 106", (unsigned)m.ntris);
  CHECK(m.nfaces == 65, "fixture nfaces %u, expected 65", (unsigned)m.nfaces);
  CHECK(m.faceVertCount == 236, "fixture faceVertCount %u, expected 236",
        (unsigned)m.faceVertCount);
  CHECK(m.fattr == nullptr, "fixture fattr not nullptr (v1 exports carry no attrs)");

  // converter-parity normalization: boundRadius exactly 1.0f, the farthest
  // vertex at distance 1 within %.6f emission quantization, nothing outside.
  CHECK(m.boundRadius == 1.0f, "fixture boundRadius %f, expected 1.0", (double)m.boundRadius);
  float maxd = 0.0f;
  for (int i = 0; i < m.nverts; ++i) {
    float d = sqrtf(m.verts[i][0] * m.verts[i][0] + m.verts[i][1] * m.verts[i][1] +
                    m.verts[i][2] * m.verts[i][2]);
    if (d > maxd) maxd = d;
  }
  CHECK(maxd <= 1.0f + 1e-4f && maxd >= 1.0f - 1e-4f,
        "fixture max vertex distance %f, expected ~1.0", (double)maxd);

  // CCW-outward winding: positive signed volume (divergence theorem over
  // the fan triangles; the composite is three closed parts).
  double vol = 0.0;
  for (int t = 0; t < m.ntris; ++t) {
    const float *a = m.verts[m.tris[t][0]];
    const float *b = m.verts[m.tris[t][1]];
    const float *c = m.verts[m.tris[t][2]];
    vol += (double)a[0] * ((double)b[1] * c[2] - (double)b[2] * c[1]) +
           (double)a[1] * ((double)b[2] * c[0] - (double)b[0] * c[2]) +
           (double)a[2] * ((double)b[0] * c[1] - (double)b[1] * c[0]);
  }
  vol /= 6.0;
  CHECK(vol > 0.0, "fixture signed volume %f not positive", vol);

  // baked corner normals stay unit through the inverse-transpose transform
  // and %.6f emission.
  long badNormals = 0;
  for (int i = 0; i < 3 * m.ntris; ++i) {
    float len = sqrtf(m.cnorms[i][0] * m.cnorms[i][0] + m.cnorms[i][1] * m.cnorms[i][1] +
                      m.cnorms[i][2] * m.cnorms[i][2]);
    if (fabsf(len - 1.0f) > 1e-4f) ++badNormals;
  }
  CHECK(badNormals == 0, "%ld fixture corner normals are not unit length", badNormals);

  // the header renders as one object through the standard scene path with a
  // nonzero draw count and lit coverage (the usage-snippet configuration).
  {
    TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);
    TD_Scene scene;
    TD_Object obj(export_fixture_mesh);
    obj.setScale(0.943039f);  // authored size, from the header's usage snippet
    scene.add(obj);
    long oobBefore = oobCount;
    clearFb();
    bool ok = scene.render(target);
    CHECK(ok, "fixture render returned false (status %d)", (int)scene.lastStatus());
    CHECK(scene.renderer().lastDrawCount() > 0, "fixture rendered zero faces");
    Stats s = stats();
    CHECK(s.count > 0, "fixture rendered zero lit pixels");
    CHECK(oobCount == oobBefore, "fixture render plotted out of bounds");
  }
}
