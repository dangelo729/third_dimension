// SPDX-License-Identifier: MIT
#include "TD_Primitives.h"

#include <math.h>
#include <stdint.h>

#include "internal/td_mesh_builder.h"

// procedural primitive generators. every constructor zeroes its TD_Mesh base
// first, so an unimplemented or failed build leaves an empty mesh (zero
// counts) that renders as a safe no-op even if the caller never checks
// valid(). generation goes through TD_MeshBuilder: one heap block,
// converter-identical structure (face-major fan triangles, true polygon
// loops, per-corner normals, CCW-outward winding), one free in the
// destructor. valid() is nverts > 0 — a successful build always produces
// vertices, a failed one none.

// M_PI is not guaranteed by the C++ standard headers, so pi is pinned here.
static constexpr float kPi = 3.14159265358979323846f;

// azimuth of ring slot i out of n, increasing counterclockwise when viewed
// from -Y (positions use (cos a, y, sin a)).
static float ringAngle(uint32_t i, uint32_t n) {
  return (2.0f * kPi) * (float)i / (float)n;
}

// angular center of the ring-face spanning slots i and i+1 — the azimuth a
// flat side face's outward normal points along.
static float ringMidAngle(uint32_t i, uint32_t n) {
  return (2.0f * kPi) * ((float)i + 0.5f) / (float)n;
}

// n = p/|p|: the smooth radial normal of any surface centered on the origin.
static void radialUnit(const float p[3], float n[3]) {
  const float inv = 1.0f / sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  n[0] = p[0] * inv;
  n[1] = p[1] * inv;
  n[2] = p[2] * inv;
}

static const float kUp[3] = {0.0f, 1.0f, 0.0f};
static const float kDown[3] = {0.0f, -1.0f, 0.0f};

// ---------------------------------------------------------------------------
// box: axis-aligned, centered at the origin, spanning [-w/2, +w/2] x
// [-h/2, +h/2] x [-d/2, +d/2].
// counts: nv = 8, nt = 12, nf = 6, loop entries = 24 (six quad loops).
// boundRadius = sqrt(w*w + h*h + d*d) / 2 (half the space diagonal).
TD_BoxMesh::TD_BoxMesh(float w, float h, float d) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (w <= 0.0f || h <= 0.0f || d <= 0.0f) return;
  TD_MeshBuilder b;
  if (!b.begin(8, 12, 6, 24, false)) return;
  // corners: bottom ring 0-3 then top ring 4-7, both counterclockwise in x/z
  // when viewed from -Y.
  static const int8_t CORNER[8][3] = {
      {-1, -1, -1}, {+1, -1, -1}, {+1, -1, +1}, {-1, -1, +1},
      {-1, +1, -1}, {+1, +1, -1}, {+1, +1, +1}, {-1, +1, +1},
  };
  for (int i = 0; i < 8; ++i)
    b.addVert((float)CORNER[i][0] * (w * 0.5f), (float)CORNER[i][1] * (h * 0.5f),
              (float)CORNER[i][2] * (d * 0.5f));
  // six quads wound CCW viewed from outside, fan-triangulated in loop order.
  // hard crease: all four corners of a face carry its outward axis normal —
  // box edges are 90-degree creases, far past the 60-degree smoothing
  // threshold.
  static const uint16_t LOOP[6][4] = {
      {0, 1, 2, 3},  // -Y
      {4, 7, 6, 5},  // +Y
      {1, 5, 6, 2},  // +X
      {0, 3, 7, 4},  // -X
      {2, 6, 7, 3},  // +Z
      {0, 4, 5, 1},  // -Z
  };
  static const float NORMAL[6][3] = {
      {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {1.0f, 0.0f, 0.0f},
      {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
  };
  for (int f = 0; f < 6; ++f) {
    b.beginFace();
    for (int i = 0; i < 4; ++i) b.loopVert(LOOP[f][i]);
    b.addTri(LOOP[f][0], LOOP[f][1], LOOP[f][2], NORMAL[f], NORMAL[f], NORMAL[f]);
    b.addTri(LOOP[f][0], LOOP[f][2], LOOP[f][3], NORMAL[f], NORMAL[f], NORMAL[f]);
    b.endFace();
  }
  b.finish(*this, sqrtf(w * w + h * h + d * d) * 0.5f);
}

TD_BoxMesh::~TD_BoxMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_BoxMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// UV sphere of radius r centered at the origin; S = slices around the
// equator, T = stacks pole to pole.
// counts: nv = S*(T-1) + 2 (T-1 latitude rings of S plus 2 pole vertices),
// nt = 2*S*(T-1), nf = S*T, loop entries = S*(4*T - 2) — two triangle rows
// at 3*S each plus (T-2) quad rows at 4*S each.
// boundRadius = r (exact: every vertex lies on the sphere).
TD_SphereMesh::TD_SphereMesh(uint16_t slices, uint16_t stacks, float radius) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (radius <= 0.0f) return;
  const uint32_t S = slices < 3 ? 3u : slices;
  const uint32_t T = stacks < 2 ? 2u : stacks;
  TD_MeshBuilder b;
  if (!b.begin(S * (T - 1) + 2, 2 * S * (T - 1), S * T, S * (4 * T - 2), false)) return;
  // north pole; rings top to bottom at increasing azimuth; south pole.
  const uint16_t north = b.addVert(0.0f, radius, 0.0f);
  for (uint32_t j = 1; j < T; ++j) {
    const float phi = kPi * (float)j / (float)T;
    const float sp = sinf(phi), cp = cosf(phi);
    for (uint32_t i = 0; i < S; ++i) {
      const float a = ringAngle(i, S);
      b.addVert(radius * (sp * cosf(a)), radius * cp, radius * (sp * sinf(a)));
    }
  }
  const uint16_t south = b.addVert(0.0f, -radius, 0.0f);
  // fully smooth: every corner takes the analytic normal v/|v| of its own
  // vertex, so corners sharing a vertex always agree (no crease anywhere).
  // row 0: S triangles fanning from the north pole to ring 1 — each is the
  // pole-collapsed form of the quad row below, keeping CCW-outward winding.
  for (uint32_t i = 0; i < S; ++i) {
    const uint16_t r0 = (uint16_t)(1 + i);
    const uint16_t r1 = (uint16_t)(1 + (i + 1) % S);
    float n0[3], np[3], n1[3];
    radialUnit(b.verts[r0], n0);
    radialUnit(b.verts[north], np);
    radialUnit(b.verts[r1], n1);
    b.beginFace();
    b.loopVert(r0);
    b.loopVert(north);
    b.loopVert(r1);
    b.addTri(r0, north, r1, n0, np, n1);
    b.endFace();
  }
  // rows 1..T-2: S quads {lower_i, upper_i, upper_i+1, lower_i+1} between
  // ring k (upper) and ring k+1 (lower), indices mod S.
  for (uint32_t k = 1; k + 1 < T; ++k) {
    const uint32_t up = 1 + (k - 1) * S;
    const uint32_t lo = up + S;
    for (uint32_t i = 0; i < S; ++i) {
      const uint32_t i2 = (i + 1) % S;
      const uint16_t l0 = (uint16_t)(lo + i), u0 = (uint16_t)(up + i);
      const uint16_t u1 = (uint16_t)(up + i2), l1 = (uint16_t)(lo + i2);
      float nl0[3], nu0[3], nu1[3], nl1[3];
      radialUnit(b.verts[l0], nl0);
      radialUnit(b.verts[u0], nu0);
      radialUnit(b.verts[u1], nu1);
      radialUnit(b.verts[l1], nl1);
      b.beginFace();
      b.loopVert(l0);
      b.loopVert(u0);
      b.loopVert(u1);
      b.loopVert(l1);
      b.addTri(l0, u0, u1, nl0, nu0, nu1);
      b.addTri(l0, u1, l1, nl0, nu1, nl1);
      b.endFace();
    }
  }
  // row T-1: S triangles closing the last ring to the south pole.
  const uint32_t last = 1 + (T - 2) * S;
  for (uint32_t i = 0; i < S; ++i) {
    const uint16_t u0 = (uint16_t)(last + i);
    const uint16_t u1 = (uint16_t)(last + (i + 1) % S);
    float ns[3], n0[3], n1[3];
    radialUnit(b.verts[south], ns);
    radialUnit(b.verts[u0], n0);
    radialUnit(b.verts[u1], n1);
    b.beginFace();
    b.loopVert(south);
    b.loopVert(u0);
    b.loopVert(u1);
    b.addTri(south, u0, u1, ns, n0, n1);
    b.endFace();
  }
  b.finish(*this, radius);
}

TD_SphereMesh::~TD_SphereMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_SphereMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// ellipsoid: the UV sphere stretched to semi-axes rx, ry, rz — same topology
// (S slices, T stacks, single pole vertices) with the non-uniform scale
// baked into the vertices. its own generator, not a scaled call into the
// sphere's, so the sphere's pinned geometry can never move.
// counts: identical to TD_SphereMesh — nv = S*(T-1) + 2, nt = 2*S*(T-1),
// nf = S*T, loop entries = S*(4*T - 2).
// normals: scaling positions does not scale normals — the analytic surface
// normal at p is the normalized implicit-surface gradient
// normalize(p.x/rx^2, p.y/ry^2, p.z/rz^2), which reduces to p/|p| only when
// rx = ry = rz. poles stay exactly (0, +-1, 0).
// boundRadius = max(rx, ry, rz): exact for the analytic surface (|p| never
// exceeds the largest semi-axis), so every vertex lies on or inside it.
// vertex attainment is sampling-dependent: the poles give ry at any
// tessellation; with stacks even the equator ring gives rx (azimuth 0 is
// always slot 0) but reaches rz only when slices is a multiple of 4 (only
// then is azimuth pi/2 a slot).

// n = normalize(p * invSq) with invSq = (1/rx^2, 1/ry^2, 1/rz^2): the
// ellipsoid's smooth normal at a surface point p centered on the origin.
static void ellipsoidUnit(const float p[3], const float invSq[3], float n[3]) {
  const float x = p[0] * invSq[0], y = p[1] * invSq[1], z = p[2] * invSq[2];
  const float inv = 1.0f / sqrtf(x * x + y * y + z * z);
  n[0] = x * inv;
  n[1] = y * inv;
  n[2] = z * inv;
}

TD_EllipsoidMesh::TD_EllipsoidMesh(uint16_t slices, uint16_t stacks, float rx, float ry,
                                   float rz) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (rx <= 0.0f || ry <= 0.0f || rz <= 0.0f) return;
  const uint32_t S = slices < 3 ? 3u : slices;
  const uint32_t T = stacks < 2 ? 2u : stacks;
  TD_MeshBuilder b;
  if (!b.begin(S * (T - 1) + 2, 2 * S * (T - 1), S * T, S * (4 * T - 2), false)) return;
  const float invSq[3] = {1.0f / (rx * rx), 1.0f / (ry * ry), 1.0f / (rz * rz)};
  // north pole; rings top to bottom at increasing azimuth; south pole —
  // the sphere's vertex order with each axis scaled by its semi-axis.
  const uint16_t north = b.addVert(0.0f, ry, 0.0f);
  for (uint32_t j = 1; j < T; ++j) {
    const float phi = kPi * (float)j / (float)T;
    const float sp = sinf(phi), cp = cosf(phi);
    for (uint32_t i = 0; i < S; ++i) {
      const float a = ringAngle(i, S);
      b.addVert(rx * (sp * cosf(a)), ry * cp, rz * (sp * sinf(a)));
    }
  }
  const uint16_t south = b.addVert(0.0f, -ry, 0.0f);
  // fully smooth: every corner takes the analytic gradient normal of its own
  // vertex, so corners sharing a vertex always agree (no crease anywhere).
  // row 0: S triangles fanning from the north pole to ring 1.
  for (uint32_t i = 0; i < S; ++i) {
    const uint16_t r0 = (uint16_t)(1 + i);
    const uint16_t r1 = (uint16_t)(1 + (i + 1) % S);
    float n0[3], np[3], n1[3];
    ellipsoidUnit(b.verts[r0], invSq, n0);
    ellipsoidUnit(b.verts[north], invSq, np);
    ellipsoidUnit(b.verts[r1], invSq, n1);
    b.beginFace();
    b.loopVert(r0);
    b.loopVert(north);
    b.loopVert(r1);
    b.addTri(r0, north, r1, n0, np, n1);
    b.endFace();
  }
  // rows 1..T-2: S quads {lower_i, upper_i, upper_i+1, lower_i+1} between
  // ring k (upper) and ring k+1 (lower), indices mod S.
  for (uint32_t k = 1; k + 1 < T; ++k) {
    const uint32_t up = 1 + (k - 1) * S;
    const uint32_t lo = up + S;
    for (uint32_t i = 0; i < S; ++i) {
      const uint32_t i2 = (i + 1) % S;
      const uint16_t l0 = (uint16_t)(lo + i), u0 = (uint16_t)(up + i);
      const uint16_t u1 = (uint16_t)(up + i2), l1 = (uint16_t)(lo + i2);
      float nl0[3], nu0[3], nu1[3], nl1[3];
      ellipsoidUnit(b.verts[l0], invSq, nl0);
      ellipsoidUnit(b.verts[u0], invSq, nu0);
      ellipsoidUnit(b.verts[u1], invSq, nu1);
      ellipsoidUnit(b.verts[l1], invSq, nl1);
      b.beginFace();
      b.loopVert(l0);
      b.loopVert(u0);
      b.loopVert(u1);
      b.loopVert(l1);
      b.addTri(l0, u0, u1, nl0, nu0, nu1);
      b.addTri(l0, u1, l1, nl0, nu1, nl1);
      b.endFace();
    }
  }
  // row T-1: S triangles closing the last ring to the south pole.
  const uint32_t last = 1 + (T - 2) * S;
  for (uint32_t i = 0; i < S; ++i) {
    const uint16_t u0 = (uint16_t)(last + i);
    const uint16_t u1 = (uint16_t)(last + (i + 1) % S);
    float ns[3], n0[3], n1[3];
    ellipsoidUnit(b.verts[south], invSq, ns);
    ellipsoidUnit(b.verts[u0], invSq, n0);
    ellipsoidUnit(b.verts[u1], invSq, n1);
    b.beginFace();
    b.loopVert(south);
    b.loopVert(u0);
    b.loopVert(u1);
    b.addTri(south, u0, u1, ns, n0, n1);
    b.endFace();
  }
  const float rmax = rx > ry ? (rx > rz ? rx : rz) : (ry > rz ? ry : rz);
  b.finish(*this, rmax);
}

TD_EllipsoidMesh::~TD_EllipsoidMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_EllipsoidMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// shared tube builder behind cylinder and prism: N-gon cross-section of
// circumradius r on the +Y axis, wall from y = -h/2 to +h/2, closed by two
// single N-gon cap loops (rim outlines carry no fan diagonals and each cap
// depth-sorts as one face). the two shapes differ only in side corner
// normals: smooth radial per vertex column, or flat per face at the quad's
// angular center. cap corners are hard (0,-1,0) / (0,+1,0) either way.
// CCW-outward rule: the -Y cap loop runs at increasing angle, the +Y cap at
// decreasing angle; side quads are {lower_i, upper_i, upper_i+1, lower_i+1}.
static void makeTube(TD_Mesh &out, uint16_t sides, float height, float radius,
                     bool smoothSides) {
  if (height <= 0.0f || radius <= 0.0f) return;
  const uint32_t N = sides < 3 ? 3u : sides;
  TD_MeshBuilder b;
  if (!b.begin(2 * N, 4 * N - 4, N + 2, 6 * N, false)) return;
  const float hy = height * 0.5f;
  for (uint32_t i = 0; i < N; ++i) {
    const float a = ringAngle(i, N);
    b.addVert(radius * cosf(a), -hy, radius * sinf(a));
  }
  for (uint32_t i = 0; i < N; ++i) {
    const float a = ringAngle(i, N);
    b.addVert(radius * cosf(a), hy, radius * sinf(a));
  }
  for (uint32_t i = 0; i < N; ++i) {
    const uint32_t j = (i + 1) % N;
    const uint16_t lo0 = (uint16_t)i, hi0 = (uint16_t)(N + i);
    const uint16_t hi1 = (uint16_t)(N + j), lo1 = (uint16_t)j;
    float n0[3], n1[3];
    if (smoothSides) {
      const float a0 = ringAngle(i, N), a1 = ringAngle(j, N);
      n0[0] = cosf(a0);
      n0[1] = 0.0f;
      n0[2] = sinf(a0);
      n1[0] = cosf(a1);
      n1[1] = 0.0f;
      n1[2] = sinf(a1);
    } else {
      const float am = ringMidAngle(i, N);
      n0[0] = n1[0] = cosf(am);
      n0[1] = n1[1] = 0.0f;
      n0[2] = n1[2] = sinf(am);
    }
    b.beginFace();
    b.loopVert(lo0);
    b.loopVert(hi0);
    b.loopVert(hi1);
    b.loopVert(lo1);
    b.addTri(lo0, hi0, hi1, n0, n0, n1);
    b.addTri(lo0, hi1, lo1, n0, n1, n1);
    b.endFace();
  }
  // -Y cap: increasing angle, fanned from its first loop vertex.
  b.beginFace();
  for (uint32_t i = 0; i < N; ++i) b.loopVert((uint16_t)i);
  for (uint32_t i = 1; i + 1 < N; ++i)
    b.addTri(0, (uint16_t)i, (uint16_t)(i + 1), kDown, kDown, kDown);
  b.endFace();
  // +Y cap: decreasing angle from slot 0, fanned from its first loop vertex.
  b.beginFace();
  b.loopVert((uint16_t)N);
  for (uint32_t i = N - 1; i >= 1; --i) b.loopVert((uint16_t)(N + i));
  for (uint32_t k = 1; k + 1 < N; ++k)
    b.addTri((uint16_t)N, (uint16_t)(N + (N - k)), (uint16_t)(N + (N - k - 1)), kUp, kUp,
             kUp);
  b.endFace();
  b.finish(out, sqrtf(radius * radius + height * height * 0.25f));
}

// ---------------------------------------------------------------------------
// cylinder: right circular tube on the +Y axis, centered at the origin —
// N-gon cross-section of circumradius r, wall from y = -h/2 to +h/2, closed
// by two N-gon caps.
// counts (N = sides): nv = 2*N, nt = 4*N - 4 (N side quads at 2 tris each +
// two caps at N-2 fan tris each), nf = N + 2, loop entries = 6*N (N quads
// at 4 + two N-gon caps at N each).
// boundRadius = sqrt(r*r + h*h/4) (distance to a rim vertex).
// wall corners stay smooth-radial at every N, even sides < 6 where a
// 60-degree crease pass would facet — TD_PrismMesh is the faceted look.
TD_CylinderMesh::TD_CylinderMesh(uint16_t sides, float height, float radius) {
  *(TD_Mesh *)this = TD_Mesh{};
  makeTube(*this, sides, height, radius, true);
}

TD_CylinderMesh::~TD_CylinderMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_CylinderMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// cone: apex up at (0, +h/2, 0), N-gon base rim of circumradius r at
// y = -h/2, closed by a base cap.
// counts (N = sides): nv = N + 1 (base ring + apex), nt = 2*N - 2 (N flank
// triangles + N-2 base fan tris), nf = N + 1 (N flanks + base),
// loop entries = 4*N (N triangle loops at 3 + one N-gon base at N).
// boundRadius = sqrt(r*r + h*h/4) (rim vertex; the apex at h/2 is nearer).

// exact lateral-surface normal of the cone at azimuth a:
// normalize(h*cos a, r, h*sin a) — its y component is r/sqrt(r*r + h*h) at
// every azimuth.
static void coneFlankNormal(float a, float h, float r, float n[3]) {
  const float x = h * cosf(a), y = r, z = h * sinf(a);
  const float inv = 1.0f / sqrtf(x * x + y * y + z * z);
  n[0] = x * inv;
  n[1] = y * inv;
  n[2] = z * inv;
}

TD_ConeMesh::TD_ConeMesh(uint16_t sides, float height, float radius) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (height <= 0.0f || radius <= 0.0f) return;
  const uint32_t N = sides < 3 ? 3u : sides;
  TD_MeshBuilder b;
  if (!b.begin(N + 1, 2 * N - 2, N + 1, 4 * N, false)) return;
  const float hy = height * 0.5f;
  for (uint32_t i = 0; i < N; ++i) {
    const float a = ringAngle(i, N);
    b.addVert(radius * cosf(a), -hy, radius * sinf(a));
  }
  const uint16_t apex = b.addVert(0.0f, hy, 0.0f);
  // flanks: loop {ring_i+1, ring_i, apex} (CCW from outside, consistent with
  // the base loop). rim corners take the surface normal at their own angle;
  // the apex corner takes it at the face's angular center — never (0,1,0) —
  // so the apex carries no highlight singularity.
  for (uint32_t i = 0; i < N; ++i) {
    const uint32_t j = (i + 1) % N;
    float ni[3], nj[3], nc[3];
    coneFlankNormal(ringAngle(i, N), height, radius, ni);
    coneFlankNormal(ringAngle(j, N), height, radius, nj);
    coneFlankNormal(ringMidAngle(i, N), height, radius, nc);
    b.beginFace();
    b.loopVert((uint16_t)j);
    b.loopVert((uint16_t)i);
    b.loopVert(apex);
    b.addTri((uint16_t)j, (uint16_t)i, apex, nj, ni, nc);
    b.endFace();
  }
  // base cap: one N-gon loop at increasing angle (CCW-outward for -Y), hard
  // (0,-1,0) corners, fanned from its first loop vertex.
  b.beginFace();
  for (uint32_t i = 0; i < N; ++i) b.loopVert((uint16_t)i);
  for (uint32_t i = 1; i + 1 < N; ++i)
    b.addTri(0, (uint16_t)i, (uint16_t)(i + 1), kDown, kDown, kDown);
  b.endFace();
  b.finish(*this, sqrtf(radius * radius + height * height * 0.25f));
}

TD_ConeMesh::~TD_ConeMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_ConeMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// prism: identical geometry to the cylinder (N-gon tube of circumradius r,
// height h, two N-gon caps) but flat-shaded walls — each side quad carries
// its own outward per-face normal at all four corners, so adjacent walls
// meet at hard creases (the faceted look).
// counts (N = sides): nv = 2*N, nt = 4*N - 4, nf = N + 2, loop entries =
// 6*N — same as cylinder.
// boundRadius = sqrt(r*r + h*h/4) (r is the circumradius, so rim corners
// are exactly at r).
TD_PrismMesh::TD_PrismMesh(uint16_t sides, float height, float radius) {
  *(TD_Mesh *)this = TD_Mesh{};
  makeTube(*this, sides, height, radius, false);
}

TD_PrismMesh::~TD_PrismMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_PrismMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// torus: ring (major) radius R around the +Y axis, tube (minor) radius rt;
// U = segU segments around the ring, V = segV around the tube.
// counts: nv = U*V, nt = 2*U*V, nf = U*V, loop entries = 4*U*V (all quads;
// the surface is closed in both directions — every index wraps mod U/mod V).
// boundRadius = R + rt (outer equator).

// fully smooth analytic normal at major slot u, minor slot v: the unit
// vector from the tube-center circle to the vertex, (cosB*cosA, sinB,
// cosB*sinA).
static void torusNormal(uint32_t u, uint32_t nu, uint32_t v, uint32_t nv, float n[3]) {
  const float A = ringAngle(u, nu);
  const float B = ringAngle(v, nv);
  const float cb = cosf(B);
  n[0] = cb * cosf(A);
  n[1] = sinf(B);
  n[2] = cb * sinf(A);
}

TD_TorusMesh::TD_TorusMesh(uint16_t segU, uint16_t segV, float tubeRadius,
                           float ringRadius) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (!(tubeRadius > 0.0f) || !(ringRadius > tubeRadius)) return;
  const uint32_t U = segU < 3 ? 3u : segU;
  const uint32_t V = segV < 3 ? 3u : segV;
  TD_MeshBuilder b;
  if (!b.begin(U * V, 2 * U * V, U * V, 4 * U * V, false)) return;
  // vertex (u, v) at major angle A and minor angle B: the tube center is
  // c = (R*cosA, 0, R*sinA) and p = c + rt*(cosB*(cosA, 0, sinA) + sinB*Y).
  for (uint32_t u = 0; u < U; ++u) {
    const float A = ringAngle(u, U);
    const float ca = cosf(A), sa = sinf(A);
    for (uint32_t v = 0; v < V; ++v) {
      const float B = ringAngle(v, V);
      const float cb = cosf(B), sb = sinf(B);
      b.addVert(ringRadius * ca + tubeRadius * (cb * ca), tubeRadius * sb,
                ringRadius * sa + tubeRadius * (cb * sa));
    }
  }
  // U*V quads joining tube ring u to u+1 and minor step v to v+1 (both mod);
  // the minor step leads the loop so the winding is CCW viewed from outside.
  for (uint32_t u = 0; u < U; ++u) {
    const uint32_t u2 = (u + 1) % U;
    for (uint32_t v = 0; v < V; ++v) {
      const uint32_t v2 = (v + 1) % V;
      const uint16_t i00 = (uint16_t)(u * V + v), i01 = (uint16_t)(u * V + v2);
      const uint16_t i11 = (uint16_t)(u2 * V + v2), i10 = (uint16_t)(u2 * V + v);
      float n00[3], n01[3], n11[3], n10[3];
      torusNormal(u, U, v, V, n00);
      torusNormal(u, U, v2, V, n01);
      torusNormal(u2, U, v2, V, n11);
      torusNormal(u2, U, v, V, n10);
      b.beginFace();
      b.loopVert(i00);
      b.loopVert(i01);
      b.loopVert(i11);
      b.loopVert(i10);
      b.addTri(i00, i01, i11, n00, n01, n11);
      b.addTri(i00, i11, i10, n00, n11, n10);
      b.endFace();
    }
  }
  b.finish(*this, ringRadius + tubeRadius);
}

TD_TorusMesh::~TD_TorusMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_TorusMesh::valid() const { return nverts > 0; }

// ---------------------------------------------------------------------------
// plane: single-sided XZ grid at y = 0 facing +Y, spanning [-w/2, +w/2] x
// [-d/2, +d/2], subdivided C x R (subdivision exists for Gouraud gradients,
// wireframe grids, and as the painter mis-sort mitigation for large floors).
// open mesh: back-face culled from below by design; exempt from the
// closed-mesh volume/manifold invariants.
// counts: nv = (C+1)*(R+1), nt = 2*C*R, nf = C*R, loop entries = 4*C*R.
// boundRadius = sqrt(w*w + d*d) / 2 (half the grid diagonal; the grid lies
// in a plane through the origin).
TD_PlaneMesh::TD_PlaneMesh(uint16_t cols, uint16_t rows, float width, float depth) {
  *(TD_Mesh *)this = TD_Mesh{};
  if (width <= 0.0f || depth <= 0.0f) return;
  const uint32_t C = cols < 1 ? 1u : cols;
  const uint32_t R = rows < 1 ? 1u : rows;
  TD_MeshBuilder b;
  if (!b.begin((C + 1) * (R + 1), 2 * C * R, C * R, 4 * C * R, false)) return;
  // grid points row-major: x from -w/2 to +w/2 across the columns, z from
  // -d/2 to +d/2 down the rows.
  for (uint32_t r = 0; r <= R; ++r) {
    const float z = depth * (float)r / (float)R - depth * 0.5f;
    for (uint32_t c = 0; c <= C; ++c) {
      const float x = width * (float)c / (float)C - width * 0.5f;
      b.addVert(x, 0.0f, z);
    }
  }
  // one quad per cell, wound CCW viewed from +Y (the visible side), every
  // corner (0,+1,0) — the whole sheet is one smooth surface. no wrapping:
  // the grid is open at its border.
  for (uint32_t r = 0; r < R; ++r)
    for (uint32_t c = 0; c < C; ++c) {
      const uint16_t v00 = (uint16_t)(r * (C + 1) + c);
      const uint16_t v10 = (uint16_t)((r + 1) * (C + 1) + c);
      const uint16_t v11 = (uint16_t)(v10 + 1);
      const uint16_t v01 = (uint16_t)(v00 + 1);
      b.beginFace();
      b.loopVert(v00);
      b.loopVert(v10);
      b.loopVert(v11);
      b.loopVert(v01);
      b.addTri(v00, v10, v11, kUp, kUp, kUp);
      b.addTri(v00, v11, v01, kUp, kUp, kUp);
      b.endFace();
    }
  b.finish(*this, sqrtf(width * width + depth * depth) * 0.5f);
}

TD_PlaneMesh::~TD_PlaneMesh() { TD_MeshBuilder::freeMesh(*this); }

bool TD_PlaneMesh::valid() const { return nverts > 0; }
