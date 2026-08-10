// SPDX-License-Identifier: MIT
// decimated-fixture suite: TD_Model_DecimatedFixture.h is a checked-in
// header baked by tools/editor.html (a molded sphere decimated to 240 tris
// at export, on an undecimated box base) and is the permanent regression
// fixture for the editor's QEM decimation path: it must keep compiling
// against TD mesh format 1, keep passing td_validateMesh, keep the
// converter-parity normalization invariants (boundRadius 1.0, farthest
// vertex at distance 1, positive signed volume, unit corner normals), keep
// the decimated part's all-triangle face structure next to the box's
// quads, and keep rendering with a nonzero draw count.

#include <cmath>

#include "TD_MonoTarget.h"
#include "TD_Scene.h"

#include "TD_Model_DecimatedFixture.h"
#include "test_common.h"

void runDecimatedFixtureTests() {
  const TD_Mesh &m = decimated_fixture_mesh;

  // structural validity plus the exact counts the scene bakes to: the
  // sphere hit its 240-tri target exactly, the box adds 12 (a count drift
  // means the QEM collapse order or the merge changed behavior).
  CHECK(td_validateMesh(m), "decimated fixture failed td_validateMesh");
  CHECK(m.nverts == 130, "fixture nverts %u, expected 130", (unsigned)m.nverts);
  CHECK(m.ntris == 252, "fixture ntris %u, expected 252 (240 decimated + 12 box)",
        (unsigned)m.ntris);
  CHECK(m.nfaces == 246, "fixture nfaces %u, expected 246", (unsigned)m.nfaces);
  CHECK(m.faceVertCount == 744, "fixture faceVertCount %u, expected 744",
        (unsigned)m.faceVertCount);
  CHECK(m.fattr == nullptr, "fixture fattr not nullptr (v1 exports carry no attrs)");

  // decimated faces are single triangles (polygon loops do not survive edge
  // collapse); the box behind them keeps its six quad loops — both face
  // shapes must coexist in one baked mesh. Bounds clamp to nfaces so a
  // count regression above still yields a clean CHECK report, never an
  // out-of-bounds read (CHECK is non-fatal by design).
  long badTriFaces = 0;
  for (int f = 0; f < 240 && f < m.nfaces; ++f)
    if (m.faces[f][1] != 1 || m.faces[f][3] != 3) ++badTriFaces;
  CHECK(badTriFaces == 0, "%ld decimated faces are not single triangles", badTriFaces);
  long badQuads = 0;
  for (int f = 240; f < 246 && f < m.nfaces; ++f)
    if (m.faces[f][1] != 2 || m.faces[f][3] != 4) ++badQuads;
  CHECK(badQuads == 0, "%ld box faces are not quads", badQuads);

  // converter-parity normalization: boundRadius exactly 1.0f, the farthest
  // vertex at distance 1 within %.6f emission quantization.
  CHECK(m.boundRadius == 1.0f, "fixture boundRadius %f, expected 1.0", (double)m.boundRadius);
  float maxd = 0.0f;
  for (int i = 0; i < m.nverts; ++i) {
    float d = sqrtf(m.verts[i][0] * m.verts[i][0] + m.verts[i][1] * m.verts[i][1] +
                    m.verts[i][2] * m.verts[i][2]);
    if (d > maxd) maxd = d;
  }
  CHECK(maxd <= 1.0f + 1e-4f && maxd >= 1.0f - 1e-4f,
        "fixture max vertex distance %f, expected ~1.0", (double)maxd);

  // CCW-outward winding: positive signed volume (divergence theorem; the
  // composite is two closed parts, and QEM must not flip either).
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

  // re-smoothed corner normals stay unit through the inverse-transpose
  // transform and %.6f emission.
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
    TD_Object obj(decimated_fixture_mesh);
    obj.setScale(0.921109f);  // authored size, from the header's usage snippet
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
