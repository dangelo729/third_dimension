// SPDX-License-Identifier: MIT
// procedural-primitive suite: the generators must emit exactly the structure
// the OBJ converter emits — count formulas, contiguous face-major partition,
// polygon loops consistent with their fan triangles, CCW-outward winding
// with positive (and where the discrete solid has one, exact) volume, unit
// analytic corner normals with the documented crease semantics — plus
// quantized golden pins, render smoke, and the no-exceptions failure paths.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "TD_Camera.h"
#include "TD_Mesh.h"
#include "TD_MonoTarget.h"
#include "TD_Primitives.h"
#include "TD_Scene.h"

#include "test_common.h"

// ---- shared helpers -------------------------------------------------------

static void checkCounts(const char *name, const TD_Mesh &m, long nv, long nt, long nf,
                        long nloop) {
  CHECK(m.nverts == nv, "%s: nverts %u, expected %ld", name, (unsigned)m.nverts, nv);
  CHECK(m.ntris == nt, "%s: ntris %u, expected %ld", name, (unsigned)m.ntris, nt);
  CHECK(m.nfaces == nf, "%s: nfaces %u, expected %ld", name, (unsigned)m.nfaces, nf);
  CHECK(m.faceVertCount == nloop, "%s: faceVertCount %u, expected %ld", name,
        (unsigned)m.faceVertCount, nloop);
}

// index ranges plus the contiguous partition invariant: faces[f] must tile
// [0,ntris) and [0,faceVertCount) in order with loopCount >= 3 — the same
// structure td_validateMesh enforces, checked independently here.
static void checkStructure(const char *name, const TD_Mesh &m) {
  CHECK(td_validateMesh(m), "%s: td_validateMesh rejected the mesh", name);
  long badIdx = 0;
  for (int t = 0; t < m.ntris; ++t)
    for (int k = 0; k < 3; ++k)
      if (m.tris[t][k] >= m.nverts) ++badIdx;
  for (int i = 0; i < m.faceVertCount; ++i)
    if (m.faceVerts[i] >= m.nverts) ++badIdx;
  CHECK(badIdx == 0, "%s: %ld out-of-range vertex indices", name, badIdx);
  long badFace = 0;
  long triAt = 0, loopAt = 0;
  for (int f = 0; f < m.nfaces; ++f) {
    if (m.faces[f][0] != triAt || m.faces[f][2] != loopAt || m.faces[f][3] < 3) ++badFace;
    triAt += m.faces[f][1];
    loopAt += m.faces[f][3];
  }
  CHECK(badFace == 0 && triAt == m.ntris && loopAt == m.faceVertCount,
        "%s: faces don't tile the tri/loop tables (%ld bad, tri end %ld, loop end %ld)",
        name, badFace, triAt, loopAt);
}

// fan consistency: each face's triangle range is a fan over its polygon
// loop — triCount == loopCount - 2 and every fan-triangle vertex is a loop
// member (SOLID/WIREFRAME outline the loop; the fill must cover exactly it).
static void checkLoopFan(const char *name, const TD_Mesh &m) {
  long bad = 0;
  for (int f = 0; f < m.nfaces; ++f) {
    int triStart = m.faces[f][0], triCount = m.faces[f][1];
    int loopStart = m.faces[f][2], loopCount = m.faces[f][3];
    if (triCount != loopCount - 2) {
      ++bad;
      continue;
    }
    for (int t = triStart; t < triStart + triCount; ++t)
      for (int k = 0; k < 3; ++k) {
        bool member = false;
        for (int i = loopStart; i < loopStart + loopCount; ++i)
          if (m.faceVerts[i] == m.tris[t][k]) member = true;
        if (!member) ++bad;
      }
  }
  CHECK(bad == 0, "%s: fan triangulation inconsistent with polygon loops (%ld)", name, bad);
}

// divergence-theorem volume over the fan triangles; positive iff the mesh is
// closed and wound CCW-outward.
static double signedVolume(const TD_Mesh &m) {
  double vol = 0.0;
  for (int t = 0; t < m.ntris; ++t) {
    const float *a = m.verts[m.tris[t][0]];
    const float *b = m.verts[m.tris[t][1]];
    const float *c = m.verts[m.tris[t][2]];
    vol += (double)a[0] * ((double)b[1] * c[2] - (double)b[2] * c[1]) +
           (double)a[1] * ((double)b[2] * c[0] - (double)b[0] * c[2]) +
           (double)a[2] * ((double)b[0] * c[1] - (double)b[1] * c[0]);
  }
  return vol / 6.0;
}

// directed-edge scan over the polygon loops. in a closed orientable
// 2-manifold every directed edge appears exactly once and its reverse
// exactly once; boundary edges (reverse missing) mark an open mesh.
static void edgeScan(const TD_Mesh &m, long &dupes, long &boundary) {
  std::vector<uint32_t> edges;
  for (int f = 0; f < m.nfaces; ++f) {
    int loopStart = m.faces[f][2], loopCount = m.faces[f][3];
    for (int i = 0; i < loopCount; ++i) {
      uint32_t a = m.faceVerts[loopStart + i];
      uint32_t b = m.faceVerts[loopStart + (i + 1) % loopCount];
      edges.push_back((a << 16) | b);
    }
  }
  std::sort(edges.begin(), edges.end());
  dupes = 0;
  for (size_t i = 1; i < edges.size(); ++i)
    if (edges[i] == edges[i - 1]) ++dupes;
  boundary = 0;
  for (uint32_t e : edges) {
    uint32_t rev = ((e & 0xFFFFu) << 16) | (e >> 16);
    if (!std::binary_search(edges.begin(), edges.end(), rev)) ++boundary;
  }
}

static float vlen3(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float vdot3(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// FNV-1a helpers for the layer-3 goldens: u16 arrays hashed raw (the pinned
// CI toolchain is little-endian), floats quantized to lround(x * 1e4) so
// last-ulp libm trig noise can't shift the hash.
static void fnvBytes(uint64_t &h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ULL;
  }
}

static void fnvQuantFloat(uint64_t &h, float v) {
  int32_t q = (int32_t)std::lround((double)v * 10000.0);
  uint8_t b[4] = {(uint8_t)(q & 0xFF), (uint8_t)((q >> 8) & 0xFF),
                  (uint8_t)((q >> 16) & 0xFF), (uint8_t)((q >> 24) & 0xFF)};
  fnvBytes(h, b, 4);
}

static uint64_t meshGoldenHash(const TD_Mesh &m) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
  uint16_t counts[4] = {m.nverts, m.ntris, m.nfaces, m.faceVertCount};
  fnvBytes(h, counts, sizeof(counts));
  for (int v = 0; v < m.nverts; ++v)
    for (int k = 0; k < 3; ++k) fnvQuantFloat(h, m.verts[v][k]);
  for (int c = 0; c < 3 * m.ntris; ++c)
    for (int k = 0; k < 3; ++k) fnvQuantFloat(h, m.cnorms[c][k]);
  for (int t = 0; t < m.ntris; ++t) fnvBytes(h, m.tris[t], sizeof(uint16_t) * 3);
  for (int f = 0; f < m.nfaces; ++f) fnvBytes(h, m.faces[f], sizeof(uint16_t) * 4);
  fnvBytes(h, m.faceVerts, sizeof(uint16_t) * m.faceVertCount);
  fnvQuantFloat(h, m.boundRadius);
  return h;
}

// smoke-test camera: explicit pose fitting the mesh's bound sphere so the
// checks are independent of auto-fit resolution.
static void setSmokeCamera(TD_Camera &cam, const TD_Mesh &m) {
  cam.lookAt({0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 0.0f});
  cam.setFocalPixels(TD_Camera::focalForFit(td_meshBoundRadius(m), 3.0f, 12.0f));
  cam.setCenterPixels(W * 0.5f, H * 0.5f);
  cam.setNearSkip(0.25f);
}

// ---- the suite -------------------------------------------------------------

void runPrimitiveTests() {
  TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);

  // default-parameter instances (every default fits the unit cube).
  TD_BoxMesh box;
  TD_SphereMesh sphere;
  TD_CylinderMesh cylinder;
  TD_ConeMesh cone;
  TD_PrismMesh prism;
  TD_TorusMesh torus;
  TD_PlaneMesh plane;
  const TD_Mesh *defaults[7] = {&box, &sphere, &cylinder, &cone, &prism, &torus, &plane};
  static const char *const NAMES[7] = {"box",   "sphere", "cylinder", "cone",
                                       "prism", "torus",  "plane"};
  CHECK(box.valid(), "box: default construction failed");
  CHECK(sphere.valid(), "sphere: default construction failed");
  CHECK(cylinder.valid(), "cylinder: default construction failed");
  CHECK(cone.valid(), "cone: default construction failed");
  CHECK(prism.valid(), "prism: default construction failed");
  CHECK(torus.valid(), "torus: default construction failed");
  CHECK(plane.valid(), "plane: default construction failed");

  // ---- P1. count formulas, swept over tessellation parameters ----
  // box 8/12/6/24 · uvSphere(S,T) S(T-1)+2 / 2S(T-1) / ST / S(4T-2)
  // cylinder,prism(N) 2N / 4N-4 / N+2 / 6N · cone(N) N+1 / 2N-2 / N+1 / 4N
  // torus(U,V) UV / 2UV / UV / 4UV · plane(C,R) (C+1)(R+1) / 2CR / CR / 4CR
  {
    checkCounts("box", box, 8, 12, 6, 24);
    static const int SL[4] = {3, 4, 16, 33};
    static const int ST[4] = {2, 3, 8, 12};
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) {
        long S = SL[i], T = ST[j];
        TD_SphereMesh m((uint16_t)S, (uint16_t)T);
        char nm[48];
        std::snprintf(nm, sizeof(nm), "sphere(%ld,%ld)", S, T);
        checkCounts(nm, m, S * (T - 1) + 2, 2 * S * (T - 1), S * T, S * (4 * T - 2));
      }
    static const int SIDES[5] = {3, 4, 8, 16, 64};
    for (int i = 0; i < 5; ++i) {
      long N = SIDES[i];
      char nm[48];
      TD_CylinderMesh cy((uint16_t)N);
      std::snprintf(nm, sizeof(nm), "cylinder(%ld)", N);
      checkCounts(nm, cy, 2 * N, 4 * N - 4, N + 2, 6 * N);
      TD_ConeMesh co((uint16_t)N);
      std::snprintf(nm, sizeof(nm), "cone(%ld)", N);
      checkCounts(nm, co, N + 1, 2 * N - 2, N + 1, 4 * N);
      TD_PrismMesh pr((uint16_t)N);
      std::snprintf(nm, sizeof(nm), "prism(%ld)", N);
      checkCounts(nm, pr, 2 * N, 4 * N - 4, N + 2, 6 * N);
    }
    static const int TU[4] = {3, 8, 16, 32};
    static const int TV[4] = {3, 4, 8, 16};
    for (int i = 0; i < 4; ++i) {
      long U = TU[i], V = TV[i];
      TD_TorusMesh m((uint16_t)U, (uint16_t)V);
      char nm[48];
      std::snprintf(nm, sizeof(nm), "torus(%ld,%ld)", U, V);
      checkCounts(nm, m, U * V, 2 * U * V, U * V, 4 * U * V);
    }
    static const int PC[3] = {1, 2, 8};
    static const int PR[3] = {1, 3, 8};
    for (int i = 0; i < 3; ++i) {
      long C = PC[i], R = PR[i];
      TD_PlaneMesh m((uint16_t)C, (uint16_t)R);
      char nm[48];
      std::snprintf(nm, sizeof(nm), "plane(%ld,%ld)", C, R);
      checkCounts(nm, m, (C + 1) * (R + 1), 2 * C * R, C * R, 4 * C * R);
    }
  }

  // ---- P2. index bounds, structural partition, bound radius ----
  for (int i = 0; i < 7; ++i) {
    const TD_Mesh &m = *defaults[i];
    checkStructure(NAMES[i], m);
    float maxLen = 0.0f;
    for (int v = 0; v < m.nverts; ++v) {
      float len = vlen3(m.verts[v]);
      if (len > maxLen) maxLen = len;
    }
    CHECK(maxLen <= m.boundRadius + 1e-4f,
          "%s: vertex at %.5f outside boundRadius %.5f", NAMES[i], maxLen, m.boundRadius);
    // exact for shapes whose farthest feature is a vertex; the torus rim may
    // fall between tube samples, so it only gets the containment check above
    if (defaults[i] != &torus)
      CHECK(std::fabs(m.boundRadius - maxLen) <= 1e-4f,
            "%s: boundRadius %.5f not tight against max vertex %.5f", NAMES[i],
            m.boundRadius, maxLen);
  }

  // ---- P3. polygon loops consistent with their fan triangles ----
  for (int i = 0; i < 7; ++i) checkLoopFan(NAMES[i], *defaults[i]);

  // ---- P4. winding: positive signed volume, exact where analytic ----
  // box w*h*d; cylinder 0.5*N*r^2*sin(2pi/N)*h (the N-gon prism it actually
  // is); cone exactly a third of that pyramid-wise.
  {
    for (int i = 0; i < 7; ++i)
      if (defaults[i] != &plane)
        CHECK(signedVolume(*defaults[i]) > 1e-4,
              "%s: signed volume %.6f not positive (winding broke)", NAMES[i],
              signedVolume(*defaults[i]));

    TD_BoxMesh vbox(0.8f, 1.2f, 0.5f);
    CHECK(std::fabs(signedVolume(vbox) - 0.8 * 1.2 * 0.5) <= 1e-4,
          "box(0.8,1.2,0.5): volume %.6f != %.6f", signedVolume(vbox), 0.8 * 1.2 * 0.5);
    const int N = 8;
    const double r = 0.4, hgt = 0.8;
    const double vcylExpect = 0.5 * N * r * r * std::sin(2.0 * M_PI / N) * hgt;
    TD_CylinderMesh vcyl((uint16_t)N, (float)hgt, (float)r);
    CHECK(std::fabs(signedVolume(vcyl) - vcylExpect) <= 1e-4,
          "cylinder(8,0.8,0.4): volume %.6f != %.6f", signedVolume(vcyl), vcylExpect);
    TD_ConeMesh vcone((uint16_t)N, (float)hgt, (float)r);
    CHECK(std::fabs(signedVolume(vcone) - vcylExpect / 3.0) <= 1e-4,
          "cone(8,0.8,0.4): volume %.6f != %.6f", signedVolume(vcone), vcylExpect / 3.0);
  }

  // ---- P5. closed meshes are directed-edge manifolds ----
  for (int i = 0; i < 7; ++i) {
    if (defaults[i] == &plane) continue;  // open by design
    long dupes = 0, boundary = 0;
    edgeScan(*defaults[i], dupes, boundary);
    CHECK(dupes == 0, "%s: %ld duplicated directed edges", NAMES[i], dupes);
    CHECK(boundary == 0, "%s: %ld boundary edges in a closed solid", NAMES[i], boundary);
  }

  // ---- P6. corner normals: unit length, analytic spot checks ----
  {
    for (int i = 0; i < 7; ++i) {
      const TD_Mesh &m = *defaults[i];
      long bad = 0;
      for (int c = 0; c < 3 * m.ntris; ++c)
        if (std::fabs(vlen3(m.cnorms[c]) - 1.0f) > 2e-3f) ++bad;
      CHECK(bad == 0, "%s: %ld non-unit corner normals", NAMES[i], bad);
    }
    // sphere: n = v/|v| at every corner
    {
      long bad = 0;
      for (int t = 0; t < sphere.ntris; ++t)
        for (int k = 0; k < 3; ++k) {
          const float *v = sphere.verts[sphere.tris[t][k]];
          const float *n = sphere.cnorms[3 * t + k];
          float len = vlen3(v);
          if (len < 1e-6f || vdot3(n, v) / len < 0.9999f) ++bad;
        }
      CHECK(bad == 0, "sphere: %ld corners off the radial normal", bad);
    }
    // box: every corner normal is a unit coordinate axis (hard creases)
    {
      long bad = 0;
      for (int c = 0; c < 3 * box.ntris; ++c) {
        const float *n = box.cnorms[c];
        int big = 0, small = 0;
        for (int k = 0; k < 3; ++k) {
          if (std::fabs(n[k]) > 0.999f) ++big;
          if (std::fabs(n[k]) < 1e-3f) ++small;
        }
        if (big != 1 || small != 2) ++bad;
      }
      CHECK(bad == 0, "box: %ld corner normals not axis-aligned", bad);
    }
    // cone flanks (loopCount 3): exact surface normal, n.y = r/sqrt(r^2+h^2)
    // at every flank corner — including the apex, which must take the face's
    // angular-center normal, not the degenerate (0,1,0)
    {
      const float expectY = 0.5f / sqrtf(0.5f * 0.5f + 1.0f);
      long bad = 0;
      for (int f = 0; f < cone.nfaces; ++f) {
        if (cone.faces[f][3] != 3) continue;  // base n-gon handled below
        int triStart = cone.faces[f][0], triCount = cone.faces[f][1];
        for (int t = triStart; t < triStart + triCount; ++t)
          for (int k = 0; k < 3; ++k)
            if (std::fabs(cone.cnorms[3 * t + k][1] - expectY) > 1e-3f) ++bad;
      }
      CHECK(bad == 0, "cone: %ld flank corners off the exact surface slope", bad);
      long badBase = 0;
      for (int f = 0; f < cone.nfaces; ++f) {
        if (cone.faces[f][3] == 3) continue;
        int triStart = cone.faces[f][0], triCount = cone.faces[f][1];
        for (int t = triStart; t < triStart + triCount; ++t)
          for (int k = 0; k < 3; ++k)
            if (cone.cnorms[3 * t + k][1] > -0.999f) ++badBase;
      }
      CHECK(badBase == 0, "cone: %ld base corners not hard -Y", badBase);
    }
  }

  // ---- P7. crease semantics ----
  {
    // cylinder: smooth radial side normals, hard +-Y caps — the same
    // position carries different normals on the wall and on the cap
    TD_CylinderMesh cyl8(8, 1.0f, 0.5f);
    long badCap = 0, badSide = 0;
    for (int f = 0; f < cyl8.nfaces; ++f) {
      int triStart = cyl8.faces[f][0], triCount = cyl8.faces[f][1];
      int loopCount = cyl8.faces[f][3];
      bool cap = loopCount == 8;
      for (int t = triStart; t < triStart + triCount; ++t)
        for (int k = 0; k < 3; ++k) {
          const float *v = cyl8.verts[cyl8.tris[t][k]];
          const float *n = cyl8.cnorms[3 * t + k];
          if (cap) {
            float sign = v[1] > 0.0f ? 1.0f : -1.0f;
            if (std::fabs(n[0]) > 1e-3f || std::fabs(n[2]) > 1e-3f ||
                std::fabs(n[1] - sign) > 1e-3f)
              ++badCap;
          } else {
            float radial[3] = {v[0], 0.0f, v[2]};
            float len = vlen3(radial);
            if (len < 1e-6f || (n[0] * radial[0] + n[2] * radial[2]) / len < 0.9999f ||
                std::fabs(n[1]) > 1e-3f)
              ++badSide;
          }
        }
    }
    CHECK(badCap == 0, "cylinder(8): %ld cap corners not hard +-Y", badCap);
    CHECK(badSide == 0, "cylinder(8): %ld side corners not smooth-radial", badSide);

    // cylinder(N<6) stays smooth where a 60-degree crease pass would facet —
    // the documented divergence from the converter (prism is the faceted
    // look). at N=4 caps and side quads share loopCount/triCount, so side
    // faces are identified geometrically: their loop spans both rim heights.
    TD_CylinderMesh cyl4(4, 1.0f, 0.5f);
    long badSmooth = 0;
    for (int f = 0; f < cyl4.nfaces; ++f) {
      int triStart = cyl4.faces[f][0], triCount = cyl4.faces[f][1];
      int loopStart = cyl4.faces[f][2], loopCount = cyl4.faces[f][3];
      bool hasTop = false, hasBottom = false;
      for (int i = loopStart; i < loopStart + loopCount; ++i) {
        if (cyl4.verts[cyl4.faceVerts[i]][1] > 0.0f) hasTop = true;
        if (cyl4.verts[cyl4.faceVerts[i]][1] < 0.0f) hasBottom = true;
      }
      if (!(hasTop && hasBottom)) continue;  // cap, not a side quad
      for (int t = triStart; t < triStart + triCount; ++t)
        for (int k = 0; k < 3; ++k) {
          const float *v = cyl4.verts[cyl4.tris[t][k]];
          const float *n = cyl4.cnorms[3 * t + k];
          float radial[3] = {v[0], 0.0f, v[2]};
          float len = vlen3(radial);
          if (len < 1e-6f || (n[0] * radial[0] + n[2] * radial[2]) / len < 0.9999f)
            ++badSmooth;
        }
    }
    CHECK(badSmooth == 0, "cylinder(4): %ld side corners faceted (must stay smooth)",
          badSmooth);

    // prism: flat per-face side normals — all corners of a side face agree
    // with each other and lie in the horizontal plane (hard vertical creases)
    TD_PrismMesh pr6(6, 1.0f, 0.5f);
    long badFlat = 0;
    for (int f = 0; f < pr6.nfaces; ++f) {
      if (pr6.faces[f][3] != 4) continue;  // caps behave like the cylinder's
      int triStart = pr6.faces[f][0], triCount = pr6.faces[f][1];
      const float *first = pr6.cnorms[3 * triStart];
      for (int t = triStart; t < triStart + triCount; ++t)
        for (int k = 0; k < 3; ++k) {
          const float *n = pr6.cnorms[3 * t + k];
          if (vdot3(n, first) < 0.99999f || std::fabs(n[1]) > 1e-3f) ++badFlat;
        }
    }
    CHECK(badFlat == 0, "prism(6): %ld side corners not flat per-face", badFlat);
  }

  // ---- P8. quantized golden hashes (layer 3) ----
  // hashes cover counts, quantized verts/normals (lround(x*1e4)), the raw
  // u16 tables, and boundRadius; a mismatch means a generator's emitted
  // geometry changed.
  {
    static const uint64_t GOLDEN_MESH[7] = {
        0x03210d47e90799c4ULL,  // box
        0xfceafa53029afc98ULL,  // sphere
        0xf59b51feec9ad5b1ULL,  // cylinder
        0x88614f65c49008aaULL,  // cone
        0x3d951575b88958c9ULL,  // prism
        0x3e8299ef21be3b43ULL,  // torus
        0xb094c18450301707ULL,  // plane
    };
    for (int i = 0; i < 7; ++i) {
      uint64_t h = meshGoldenHash(*defaults[i]);
      CHECK(h == GOLDEN_MESH[i], "%s: mesh hash 0x%llxULL != pinned 0x%llxULL", NAMES[i],
            (unsigned long long)h, (unsigned long long)GOLDEN_MESH[i]);
    }
  }

  // ---- P9. render smoke: every primitive draws through the renderer ----
  for (int i = 0; i < 7; ++i) {
    TD_Scene scene;
    TD_Object obj(*defaults[i]);
    scene.add(obj);
    setSmokeCamera(scene.camera, *defaults[i]);
    scene.setRenderMode(TD_SOLID);
    if (defaults[i] == &plane)
      obj.setRotation(-1.0f, 0.0f, 0.0f);  // tip the +Y face toward the camera
    else
      obj.setRotation(-0.5f, 0.6f, 0.0f);
    clearFb();
    bool ok = scene.render(target);
    CHECK(ok, "%s: scene.render failed (status %d)", NAMES[i], (int)scene.lastStatus());
    CHECK(stats().count > 20, "%s: render smoke drew almost nothing (%ld px)", NAMES[i],
          stats().count);
  }
  CHECK(oobCount == 0, "primitive renders plotted out of bounds %ld times", oobCount);

  // ---- P10. failure paths: clamping, invalid dimensions, u16 overflow ----
  {
    // tessellation params clamp to their minimums (always renders something)
    TD_SphereMesh sMin(0, 0);
    CHECK(sMin.valid(), "sphere(0,0): tessellation must clamp, not fail");
    checkCounts("sphere(0,0)->(3,2)", sMin, 5, 6, 6, 18);
    TD_CylinderMesh cMin(1);
    CHECK(cMin.valid(), "cylinder(1): sides must clamp to 3");
    checkCounts("cylinder(1)->(3)", cMin, 6, 8, 5, 18);
    TD_TorusMesh tMin(1, 1);
    CHECK(tMin.valid(), "torus(1,1): segments must clamp to 3");
    checkCounts("torus(1,1)->(3,3)", tMin, 9, 18, 9, 36);
    TD_PlaneMesh pMin(0, 0);
    CHECK(pMin.valid(), "plane(0,0): grid must clamp to 1x1");
    checkCounts("plane(0,0)->(1,1)", pMin, 4, 2, 1, 4);

    // non-positive dimensions are a logic error: empty mesh, valid() false
    TD_BoxMesh badBox(0.0f, 1.0f, 1.0f);
    CHECK(!badBox.valid(), "box(0,1,1) must be invalid");
    CHECK(badBox.nverts == 0 && badBox.ntris == 0 && badBox.nfaces == 0 &&
              badBox.faceVertCount == 0,
          "invalid box must have zero counts");
    TD_SphereMesh badSphere(16, 8, -0.5f);
    CHECK(!badSphere.valid(), "sphere(radius -0.5) must be invalid");
    TD_ConeMesh badCone(16, -1.0f);
    CHECK(!badCone.valid(), "cone(height -1) must be invalid");
    TD_TorusMesh badTorus(16, 8, 0.4f, 0.3f);
    CHECK(!badTorus.valid(), "torus(tube >= ring) must be invalid");
    TD_PlaneMesh badPlane(1, 1, 0.0f, 1.0f);
    CHECK(!badPlane.valid(), "plane(width 0) must be invalid");

    // u16 index overflow: torus(256,256) needs 65536 verts — one past the cap
    TD_TorusMesh huge(256, 256);
    CHECK(!huge.valid(), "torus(256,256) must fail the u16 index cap");
    CHECK(huge.nverts == 0 && huge.ntris == 0, "overflowed torus must be empty");

    // rendering an invalid mesh is a safe no-op even without checking valid()
    TD_Scene scene;
    TD_Object obj(badBox);
    scene.add(obj);
    setSmokeCamera(scene.camera, badBox);
    scene.setRenderMode(TD_SOLID);
    clearFb();
    scene.render(target);
    CHECK(stats().count == 0, "invalid mesh must render as a no-op (%ld px)", stats().count);
    CHECK(oobCount == 0, "invalid-mesh render plotted out of bounds");
  }

  // ---- P11. plane exemptions: open, single-sided, +Y-facing ----
  {
    TD_PlaneMesh grid(2, 2);
    CHECK(grid.valid(), "plane(2,2): construction failed");
    long dupes = 0, boundary = 0;
    edgeScan(grid, dupes, boundary);
    CHECK(dupes == 0, "plane(2,2): %ld duplicated directed edges", dupes);
    CHECK(boundary == 8, "plane(2,2): expected 8 boundary edges (open mesh), got %ld",
          boundary);
    long badN = 0;
    for (int c = 0; c < 3 * grid.ntris; ++c) {
      const float *n = grid.cnorms[c];
      if (std::fabs(n[0]) > 1e-4f || std::fabs(n[1] - 1.0f) > 1e-4f ||
          std::fabs(n[2]) > 1e-4f)
        ++badN;
    }
    CHECK(badN == 0, "plane(2,2): %ld corner normals not +Y", badN);

    // single-sided: visible from above, fully culled from below
    {
      TD_Scene scene;
      TD_Object obj(grid);
      scene.add(obj);
      scene.camera.lookAt({0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
      scene.camera.setFocalPixels(TD_Camera::focalForFit(td_meshBoundRadius(grid), 3.0f, 12.0f));
      scene.camera.setCenterPixels(W * 0.5f, H * 0.5f);
      scene.camera.setNearSkip(0.25f);
      scene.setRenderMode(TD_SOLID);
      clearFb();
      scene.render(target);
      CHECK(stats().count > 20, "plane: not visible from above (%ld px)", stats().count);
      scene.camera.lookAt({0.0f, -3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
      clearFb();
      scene.render(target);
      CHECK(stats().count == 0, "plane: must be back-face culled from below (%ld px)",
            stats().count);
    }
  }

  clearFb();
}
