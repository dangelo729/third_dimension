// SPDX-License-Identifier: MIT
// outline suite: the TD_OUTLINE feature-line mode and its edge-adjacency
// module. layer 1 pins td_edge_table's structure (weld, pairing, crease and
// boundary truth per primitive, non-manifold/degenerate degradation); layer 2
// pins the render path — golden frames identical across every dither method
// (only the 0.0/2.0 endpoint shades ever reach the target), the library's
// RGB565 outline pin, seam audits, feature-edge classification acceptance,
// hidden-line removal, determinism, and the opt-out identity against K5.
//
// naming note: "feature edges" / "contour" here — the suite already uses
// "silhouette" to mean the covered-pixel footprint.

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "TD_Color565Target.h"
#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "TD_Primitives.h"
#include "TD_Scene.h"
#include <internal/td_edge_table.h>
#include <models/TD_Model_Barrel.h>

#include "test_common.h"

// the K5 golden barrel fixture from test_render.cpp, reproduced exactly:
// legacy camera, oblique light, demo ambient, demo pose.
static const float TILT = -0.30f;

static void setLegacyCamera(TD_Camera &cam) {
  cam.lookAt({0.0f, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f});
  cam.setFocalPixels(13.0f * sqrtf(16.0f - 1.0f));  // fit: FIT_PX * sqrt(D*D - 1)
  cam.setNearSkip(0.25f);
  cam.setCenterPixels(W * 0.5f, H * 0.5f);
}

static void setupK5(TD_Scene &scene, TD_Object &obj) {
  scene.add(obj);
  setLegacyCamera(scene.camera);
  scene.light.setDirection(0.6f, 0.45f);
  scene.light.setIntensity(1.0f);
  scene.setAmbient(0.15f);
  obj.setRotation(TILT, 0.7f, 0.0f);
}

static bool renderTo(TD_Scene &scene, TD_RenderTarget &target, TD_RenderMode mode) {
  clearFb();
  TD_Renderer &r = scene.renderer();
  r.setMode(mode);
  return r.render(scene, scene.camera, target);
}

static void renderToChecked(TD_Scene &scene, TD_RenderTarget &target, TD_RenderMode mode) {
  bool ok = renderTo(scene, target, mode);
  CHECK(ok, "render returned false (status %d)", (int)scene.lastStatus());
}

static bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// ---- layer 1 helpers -------------------------------------------------------

// slot tallies over a built table: paired (opp set), boundary flags, crease
// flags — the whole classification surface as three counts.
struct EdgeTally {
  long paired = 0, boundary = 0, crease = 0;
};

static EdgeTally tallyTable(const TD_EdgeTable &t) {
  EdgeTally e;
  for (uint16_t i = 0; i < t.nloop; ++i) {
    if (t.opp[i] != TD_EDGE_NONE) ++e.paired;
    if (t.flags[i] & TD_EDGE_BOUNDARY) ++e.boundary;
    if (t.flags[i] & TD_EDGE_CREASE) ++e.crease;
  }
  return e;
}

static void checkTally(const char *name, const TD_Mesh &m, long paired, long boundary,
                       long crease) {
  TD_EdgeTable t;
  CHECK(t.build(m, TD_OUTLINE_CREASE_DEFAULT), "%s: edge table build failed", name);
  CHECK(t.nloop == m.faceVertCount, "%s: table nloop %u != faceVertCount %u", name,
        (unsigned)t.nloop, (unsigned)m.faceVertCount);
  EdgeTally e = tallyTable(t);
  CHECK(e.paired == paired, "%s: %ld paired slots, expected %ld", name, e.paired, paired);
  CHECK(e.boundary == boundary, "%s: %ld boundary slots, expected %ld", name, e.boundary,
        boundary);
  CHECK(e.crease == crease, "%s: %ld crease slots, expected %ld", name, e.crease, crease);
  t.release();
}

// FNV-1a over the table's integer sections — the structural golden pin (the
// float inputs only ever cross wide angular margins, so the integer output
// is platform-stable).
static uint64_t tableHash(const TD_EdgeTable &t) {
  uint64_t h = 1469598103934665603ULL;
  for (uint16_t i = 0; i < t.nloop; ++i) {
    h ^= (uint8_t)(t.opp[i] & 0xFF);
    h *= 1099511628211ULL;
    h ^= (uint8_t)(t.opp[i] >> 8);
    h *= 1099511628211ULL;
  }
  for (uint16_t i = 0; i < t.nloop; ++i) {
    h ^= t.flags[i];
    h *= 1099511628211ULL;
  }
  return h;
}

// seam recorder for the outline audits: every span and plot must arrive at
// the bitwise 0.0/2.0 endpoint sentinels with a +0.0f slope, in bounds, and
// every setMaterial must carry mode TD_OUTLINE with a BLACK/WHITE material.
struct OutlineSeamRecorder : TD_RenderTarget {
  long spanCalls = 0, plotCalls = 0, matCalls = 0;
  int badShade = 0, badSlope = 0, badMat = 0, oob = 0;

  void reset() {
    spanCalls = plotCalls = matCalls = 0;
    badShade = badSlope = badMat = oob = 0;
  }

  int16_t width() override { return W; }
  int16_t height() override { return H; }

  void setMaterial(const TD_MaterialCtx &ctx) override {
    ++matCalls;
    if (ctx.mode != (uint8_t)TD_OUTLINE ||
        (ctx.mat != TD_MATERIAL_BLACK && ctx.mat != TD_MATERIAL_WHITE))
      ++badMat;
  }

  void plotShade(int16_t x, int16_t y, float shade) override {
    ++plotCalls;
    if (x < 0 || x >= W || y < 0 || y >= H) {
      ++oob;
      return;
    }
    if (!(sameBits(shade, 0.0f) || sameBits(shade, 2.0f))) ++badShade;
  }

  void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX, float leftShade,
                     float shadeSlope) override {
    (void)leftEdgeX;
    ++spanCalls;
    if (y < 0 || y >= H || xs < 0 || xe >= W || xs > xe) {
      ++oob;
      return;
    }
    if (!sameBits(shadeSlope, 0.0f)) ++badSlope;
    if (!(sameBits(leftShade, 0.0f) || sameBits(leftShade, 2.0f))) ++badShade;
  }
};

// lit pixels strictly inside a disc — the interior census the feature-edge
// acceptance checks classify with (rim ink lives outside the disc).
static long discCount(double cx, double cy, double r) {
  long n = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (fb[y][x]) {
        double dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy < r * r) ++n;
      }
  return n;
}

// 4-connected flood of unlit pixels from the frame border; true if it reaches
// (x, y). an 8-connected closed ink ring keeps the flood out of its interior,
// so this is the closure test for a contour.
static bool floodReaches(int x, int y) {
  static uint8_t seen[H][W];
  static int16_t qx[W * H], qy[W * H];
  std::memset(seen, 0, sizeof(seen));
  int head = 0, tail = 0;
  auto push = [&](int px, int py) {
    if (px < 0 || px >= W || py < 0 || py >= H || seen[py][px] || fb[py][px]) return;
    seen[py][px] = 1;
    qx[tail] = (int16_t)px;
    qy[tail] = (int16_t)py;
    ++tail;
  };
  for (int i = 0; i < W; ++i) {
    push(i, 0);
    push(i, H - 1);
  }
  for (int j = 0; j < H; ++j) {
    push(0, j);
    push(W - 1, j);
  }
  while (head < tail) {
    int px = qx[head], py = qy[head];
    ++head;
    push(px + 1, py);
    push(px - 1, py);
    push(px, py + 1);
    push(px, py - 1);
  }
  return seen[y][x] != 0;
}

void runOutlineTests() {
  TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);

  // ---- E1. closed primitives: every slot pairs, crease truth ----
  // pairing: a closed manifold pairs all faceVertCount slots (E = fvc/2
  // edges), zero boundary. crease truth per shape intent at the 60-degree
  // default: box all-crease (90-degree dihedrals); sphere/torus none (fully
  // smooth); cylinder rims-only (wall-wall 22.5 degrees smooth, wall-cap 90
  // hard); cone base-rim only (flank-flank smooth); prism(4) all-crease
  // (sides 90 apart — prism(6) is deliberately absent: its 60-degree side
  // dihedrals sit bitwise on the converter's ties-shade-smooth razor edge).
  {
    TD_BoxMesh box;
    TD_SphereMesh sphere;
    TD_CylinderMesh cylinder;
    TD_ConeMesh cone;
    TD_PrismMesh prism4(4);
    TD_TorusMesh torus;
    checkTally("box", box, 24, 0, 24);
    checkTally("sphere(16,8)", sphere, 480, 0, 0);
    checkTally("cylinder(16)", cylinder, 96, 0, 64);
    checkTally("cone(16)", cone, 64, 0, 32);
    checkTally("prism(4)", prism4, 24, 0, 24);
    checkTally("torus(16,8)", torus, 512, 0, 0);
  }

  // ---- E2. open mesh: plane(2,2) has exactly 8 boundary edges ----
  // interior edges are coplanar (dot 1.0, smooth); the border is unpaired.
  {
    TD_PlaneMesh grid(2, 2);
    checkTally("plane(2,2)", grid, 8, 8, 0);
  }

  // ---- E3. weld: duplicated seam vertices pair, no false boundary ----
  // two quads folded 90 degrees share an edge but not vertex indices (the
  // converter's position-duplicated layout). the seam must weld, pair, and
  // crease; the 6 outer edges stay boundary.
  {
    static const float wv[8][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},          // quad A (xy plane, +z)
        {1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {1, 0, 1}};         // quad B (xz plane, +y)
    static const uint16_t wt[4][3] = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}};
    static const float wn[12][3] = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
        {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    static const uint16_t wf[2][4] = {{0, 2, 0, 4}, {2, 2, 4, 4}};
    static const uint16_t wl[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    static const TD_Mesh welded = {wv, wt, wn, wf, wl, nullptr, 8, 4, 2, 8, 1.0f};
    TD_EdgeTable t;
    CHECK(t.build(welded, TD_OUTLINE_CREASE_DEFAULT), "weld fixture: build failed");
    CHECK(t.opp[0] == 1 && t.opp[4] == 0,
          "weld: seam slots must pair across faces (opp %u / %u)", (unsigned)t.opp[0],
          (unsigned)t.opp[4]);
    CHECK((t.flags[0] & TD_EDGE_BOUNDARY) == 0 && (t.flags[4] & TD_EDGE_BOUNDARY) == 0,
          "weld: seam must not read as boundary");
    CHECK((t.flags[0] & TD_EDGE_CREASE) && (t.flags[4] & TD_EDGE_CREASE),
          "weld: the 90-degree fold must read as a crease");
    EdgeTally e = tallyTable(t);
    CHECK(e.paired == 2 && e.boundary == 6, "weld: tally %ld paired / %ld boundary != 2 / 6",
          e.paired, e.boundary);
    t.release();
  }

  // ---- E4. non-manifold: 3 faces on one edge pair first-come, extra ->
  // boundary — degrade, never assert ----
  {
    static const float nv[12][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},      // quad A
        {1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {1, 0, 1},      // quad B
        {0, 0, 0}, {1, 0, 0}, {1, -1, 0}, {0, -1, 0}};   // quad C
    static const uint16_t nt[6][3] = {{0, 1, 2},  {0, 2, 3}, {4, 5, 6},
                                      {4, 6, 7},  {8, 9, 10}, {8, 10, 11}};
    static const float nn[18][3] = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
        {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    static const uint16_t nfc[3][4] = {{0, 2, 0, 4}, {2, 2, 4, 4}, {4, 2, 8, 4}};
    static const uint16_t nl[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const TD_Mesh nonman = {nv, nt, nn, nfc, nl, nullptr, 12, 6, 3, 12, 1.0f};
    TD_EdgeTable t;
    CHECK(t.build(nonman, TD_OUTLINE_CREASE_DEFAULT), "non-manifold: build failed");
    // the shared edge appears in slots 0 (A), 4 (B), 8 (C): the two lowest
    // slots pair, the third reads boundary.
    CHECK(t.opp[0] == 1 && t.opp[4] == 0, "non-manifold: first two faces must pair");
    CHECK(t.opp[8] == TD_EDGE_NONE && (t.flags[8] & TD_EDGE_BOUNDARY),
          "non-manifold: the third face's edge must degrade to boundary");
    t.release();
  }

  // ---- E5. degenerate zero-area face: builds, degrades, never crashes ----
  {
    static const float dv[6][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0},
                                   {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    static const uint16_t dt[2][3] = {{0, 1, 2}, {3, 4, 5}};
    static const float dn[6][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1},
                                   {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    static const uint16_t df[2][4] = {{0, 1, 0, 3}, {1, 1, 3, 3}};
    static const uint16_t dl[6] = {0, 1, 2, 3, 4, 5};
    static const TD_Mesh degen = {dv, dt, dn, df, dl, nullptr, 6, 2, 2, 6, 1.0f};
    TD_EdgeTable t;
    CHECK(t.build(degen, TD_OUTLINE_CREASE_DEFAULT), "degenerate: build must not fail");
    CHECK(t.nloop == 6, "degenerate: table must cover all slots");
    t.release();
    t.release();  // idempotent on the empty state
  }

  // ---- E6. reciprocity: opp relations are symmetric on a closed solid ----
  {
    TD_BoxMesh box;
    TD_EdgeTable t;
    CHECK(t.build(box, TD_OUTLINE_CREASE_DEFAULT), "box: build failed");
    long bad = 0;
    for (uint16_t f = 0; f < box.nfaces; ++f) {
      uint16_t ls = box.faces[f][2], lc = box.faces[f][3];
      for (uint16_t k = 0; k < lc; ++k) {
        uint16_t g = t.opp[ls + k];
        if (g == TD_EDGE_NONE) {
          ++bad;
          continue;
        }
        uint16_t gs = box.faces[g][2], gc = box.faces[g][3];
        bool back = false;
        for (uint16_t j = 0; j < gc && !back; ++j) back = t.opp[gs + j] == f;
        if (!back) ++bad;
      }
    }
    CHECK(bad == 0, "box: %ld slots without a reciprocal opp", bad);
    t.release();
  }

  // ---- E7. decal faces contribute no entries ----
  // a decal quad hosted on a body face: its slots stay TD_EDGE_NONE with no
  // flags (never read — OUTLINE skips decals), and the body faces still pair
  // among themselves as if the decal did not exist.
  {
    static const float cv[12][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},                      // body A
        {1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {1, 0, 1},                      // body B
        {0.2f, 0.2f, 0}, {0.8f, 0.2f, 0}, {0.8f, 0.8f, 0}, {0.2f, 0.8f, 0}};  // decal on A
    static const uint16_t ct[6][3] = {{0, 1, 2},  {0, 2, 3},  {4, 5, 6},
                                      {4, 6, 7},  {8, 9, 10}, {8, 10, 11}};
    static const float cn[18][3] = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
        {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    static const uint16_t cf[3][4] = {{0, 2, 0, 4}, {2, 2, 4, 4}, {4, 2, 8, 4}};
    static const uint16_t cl[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const TD_FaceAttr ca[3] = {{TD_MATERIAL_LIT, 0, TD_NO_HOST},
                                      {TD_MATERIAL_LIT, 0, TD_NO_HOST},
                                      {TD_MATERIAL_WHITE, 0, 0}};
    static const TD_Mesh decaled = {cv, ct, cn, cf, cl, ca, 12, 6, 3, 12, 1.0f};
    TD_EdgeTable t;
    CHECK(t.build(decaled, TD_OUTLINE_CREASE_DEFAULT), "decal fixture: build failed");
    long decalTouched = 0;
    for (int i = 8; i < 12; ++i)
      if (t.opp[i] != TD_EDGE_NONE || t.flags[i] != 0) ++decalTouched;
    CHECK(decalTouched == 0, "decal: %ld slots carry adjacency (must contribute none)",
          decalTouched);
    CHECK(t.opp[0] == 1 && t.opp[4] == 0, "decal: body faces must still pair");
    t.release();
  }

  // ---- E8. empty mesh builds to the empty state ----
  {
    TD_Mesh empty{};
    TD_EdgeTable t;
    CHECK(t.build(empty, TD_OUTLINE_CREASE_DEFAULT), "empty mesh: build must succeed");
    CHECK(t.nloop == 0 && t.opp == nullptr, "empty mesh: table must stay empty");
    t.release();
  }

  // ---- E9. structural golden pins ----
  // FNV-1a over the opp/flags sections for box and cone(16) — pins weld
  // order, pairing, and the crease decisions in one integer signature.
  // regenerate only deliberately, never as a casual refresh.
  {
    static const uint64_t GOLDEN_TABLE_BOX = 0xa9085b460121722bULL;
    static const uint64_t GOLDEN_TABLE_CONE = 0x371596abf670e103ULL;
    TD_BoxMesh box;
    TD_ConeMesh cone;
    TD_EdgeTable t;
    CHECK(t.build(box, TD_OUTLINE_CREASE_DEFAULT), "box: build failed");
    uint64_t hb = tableHash(t);
    CHECK(hb == GOLDEN_TABLE_BOX, "box table hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)hb, (unsigned long long)GOLDEN_TABLE_BOX);
    t.release();
    CHECK(t.build(cone, TD_OUTLINE_CREASE_DEFAULT), "cone: build failed");
    uint64_t hc = tableHash(t);
    CHECK(hc == GOLDEN_TABLE_CONE, "cone table hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)hc, (unsigned long long)GOLDEN_TABLE_CONE);
    t.release();
  }

  // ---- O1. golden frames: one hash, every dither method ----
  // the K5 barrel under TD_OUTLINE. only bitwise 0.0/2.0 shades reach the
  // target (O3 audits that), and every dither method — the two diffusion
  // methods included, whose <= 0 / >= 1 taps absorb error — realizes those
  // endpoints identically, so the frame is pinned once and asserted equal
  // across all ten methods: the mode's own invariant.
  {
    static const uint64_t GOLDEN_OUTLINE_FRAME = 0xef57e91044e89d75ULL;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    for (int m = 0; m < TD_DITHER_COUNT; ++m) {
      const char *nm = td_ditherName((TD_Dither)m);
      CHECK(target.setDither((TD_Dither)m), "%s: setDither failed", nm);
      renderToChecked(scene, target, TD_OUTLINE);
      CHECK(stats().count > 0, "%s: outline render is empty", nm);
      uint64_t hsh = fbHash();
      CHECK(hsh == GOLDEN_OUTLINE_FRAME,
            "%s: outline frame hash 0x%llxULL != pinned 0x%llxULL (must be dither-invariant)",
            nm, (unsigned long long)hsh, (unsigned long long)GOLDEN_OUTLINE_FRAME);
    }
    target.setDither(TD_DITHER_BAYER8);
  }

  // ---- O2. color target: exact matBlack/matWhite palette + RGB565 pin ----
  // fills arrive as matBlack (not setBackground — the mono "background" fill
  // is a palette color on RGB565), ink as matWhite; with the default palette
  // the frame holds exactly {0x0000, 0xFFFF}.
  {
    static const uint64_t GOLDEN_OUTLINE_565 = 0x90e8f0068cad44cbULL;
    static uint16_t cfb[W * H];
    TD_Color565Target ct(cfb, (int16_t)W, (int16_t)H);
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    obj.setColor(0x07E0);  // baseColor must not reach outline pixels
    renderToChecked(scene, ct, TD_OUTLINE);
    long offPalette = 0, ink = 0;
    for (int i = 0; i < W * H; ++i) {
      if (cfb[i] == 0xFFFF) ++ink;
      else if (cfb[i] != 0x0000) ++offPalette;
    }
    CHECK(ink > 0, "565 outline frame has no matWhite ink");
    CHECK(offPalette == 0, "565 outline frame: %ld pixels off the matBlack/matWhite palette",
          offPalette);
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < W * H; ++i) {
      h ^= (uint8_t)(cfb[i] & 0xFF);
      h *= 1099511628211ULL;
      h ^= (uint8_t)(cfb[i] >> 8);
      h *= 1099511628211ULL;
    }
    CHECK(h == GOLDEN_OUTLINE_565, "565 outline frame hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)h, (unsigned long long)GOLDEN_OUTLINE_565);
  }

  // ---- O3. seam audit: only the 0.0/2.0 endpoints ever cross the seam ----
  {
    static OutlineSeamRecorder rec;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    rec.reset();
    CHECK(renderTo(scene, rec, TD_OUTLINE), "seam audit render failed (status %d)",
          (int)scene.lastStatus());
    CHECK(rec.spanCalls > 0, "outline seam audit saw no fill spans");
    CHECK(rec.plotCalls > 0, "outline ink must reach the per-pixel path");
    CHECK(rec.matCalls > 0, "outline must latch materials");
    CHECK(rec.badShade == 0, "%d seam shades off the bitwise 0.0/2.0 endpoints", rec.badShade);
    CHECK(rec.badSlope == 0, "%d fill spans with a non-+0.0f slope", rec.badSlope);
    CHECK(rec.badMat == 0, "%d setMaterial calls off mode 4 / BLACK / WHITE", rec.badMat);
    CHECK(rec.oob == 0, "%d outline seam calls out of bounds", rec.oob);
  }

  // ---- O4. WHITE faces fill solid (D2's eyeball case) ----
  // a whole-object WHITE override under OUTLINE fills at 2.0: the frame is a
  // solid silhouette (ink is invisible against the white fill), at least as
  // covered as the shaded ambient-1.0 silhouette.
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setAmbient(1.0f);
    renderToChecked(scene, target, TD_SHADED);
    long sil = stats().count;
    scene.setAmbient(0.15f);
    obj.setMaterial(TD_MATERIAL_WHITE);
    renderToChecked(scene, target, TD_OUTLINE);
    long solid = stats().count;
    CHECK(solid >= sil, "WHITE outline fill (%ld) must cover the silhouette (%ld)", solid, sil);
    CHECK(solid < sil + sil / 2, "WHITE outline fill (%ld) suspiciously large vs %ld", solid,
          sil);
  }

  // ---- O5. feature-edge classification acceptance ----
  {
    // sphere alone: contour ring only, interior clean at every yaw, ring
    // stable (the mesh is smooth — no crease flags — and rotation cannot
    // stick edges: facing is re-derived per frame).
    TD_SphereMesh sphereMesh;
    TD_ConeMesh coneMesh;
    double rimR = 0.0, rimCx = 0.0, rimCy = 0.0;
    {
      TD_Scene scene;
      TD_Object sphere(sphereMesh);
      setupK5(scene, sphere);
      sphere.setRotation(0.0f, 0.0f, 0.0f);
      renderToChecked(scene, target, TD_OUTLINE);
      Stats s = stats();
      CHECK(s.count > 20, "sphere contour suspiciously sparse (%ld px)", s.count);
      rimCx = (s.minx + s.maxx) * 0.5;
      rimCy = (s.miny + s.maxy) * 0.5;
      rimR = (s.maxx - s.minx) * 0.5;
      CHECK(discCount(rimCx, rimCy, rimR - 2.0) == 0,
            "sphere interior must be clean (%ld lit inside)",
            discCount(rimCx, rimCy, rimR - 2.0));
      for (int k = 1; k <= 6; ++k) {
        sphere.setRotation(0.3f, k * 0.9f, 0.1f);
        renderToChecked(scene, target, TD_OUTLINE);
        Stats r = stats();
        CHECK(discCount(rimCx, rimCy, rimR - 2.0) == 0,
              "yaw %d: rotating sphere interior must stay clean (%ld lit)", k,
              discCount(rimCx, rimCy, rimR - 2.0));
        CHECK(std::labs(r.maxx - s.maxx) <= 1 && std::labs(r.minx - s.minx) <= 1 &&
                  std::labs(r.maxy - s.maxy) <= 1 && std::labs(r.miny - s.miny) <= 1,
              "yaw %d: contour ring must track the sphere (bbox drifted)", k);
      }
    }

    // cone poking out of the sphere, front-on: its base-rim crease ring
    // reads as ink inside the sphere's contour ring.
    {
      TD_Scene scene;
      TD_Object sphere(sphereMesh);
      TD_Object cone(coneMesh);
      setupK5(scene, sphere);
      scene.add(cone);
      sphere.setRotation(0.0f, 0.0f, 0.0f);
      cone.setScale(0.5f);
      cone.setRotation(-(float)M_PI / 2, 0.0f, 0.0f);  // apex toward the camera
      cone.setPosition(0.0f, 0.0f, -0.8f);
      renderToChecked(scene, target, TD_OUTLINE);
      long ring = discCount(rimCx, rimCy, rimR - 2.0);
      CHECK(ring > 5, "front-on cone must ink a ring inside the contour (%ld lit inside)",
            ring);
    }

    // ~90 degrees: the cone fuses into the outline — the combined contour
    // extends past the sphere's own.
    {
      TD_Scene scene;
      TD_Object sphere(sphereMesh);
      TD_Object cone(coneMesh);
      setupK5(scene, sphere);
      scene.add(cone);
      sphere.setRotation(0.0f, 0.0f, 0.0f);
      cone.setScale(0.5f);
      cone.setRotation(0.0f, 0.0f, (float)M_PI / 2);  // apex out the -x side
      cone.setPosition(-0.55f, 0.0f, 0.0f);
      renderToChecked(scene, target, TD_OUTLINE);
      Stats s = stats();
      CHECK(s.minx <= (int)(rimCx - rimR) - 3,
            "side-on cone must extend the outline past the rim (minx %d, rim left %d)",
            s.minx, (int)(rimCx - rimR));
    }
  }

  // ---- O6. hidden-line removal: near fill erases far ink ----
  // a far box pitched/yawed so crease edges cross the center window, then a
  // near sphere whose fill footprint covers the window (its own contour ring
  // lies outside it): the window's far ink must be erased.
  {
    TD_BoxMesh boxMesh;
    TD_SphereMesh sphereMesh;
    long boxInk = 0;
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      setupK5(scene, box);
      box.setPosition(0.0f, 0.0f, 0.9f);
      renderToChecked(scene, target, TD_OUTLINE);
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) boxInk += fb[y][x];
      CHECK(boxInk > 0, "baseline: far box ink must cross the window (%ld/25)", boxInk);
    }
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      TD_Object sphere(sphereMesh);
      setupK5(scene, box);
      scene.add(sphere);
      box.setPosition(0.0f, 0.0f, 0.9f);
      sphere.setRotation(0.0f, 0.0f, 0.0f);
      sphere.setPosition(0.0f, 0.0f, -0.9f);
      renderToChecked(scene, target, TD_OUTLINE);
      long winBoth = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) winBoth += fb[y][x];
      CHECK(winBoth == 0, "near fill must erase the far ink in the window (%ld/25 lit)",
            winBoth);
      CHECK(stats().count > 0, "hidden-line scene must still draw contours");
    }
  }

  // ---- O6c. an inkless object occludes and draws no line ----
  // the O6 scene with the near sphere's ink off, and a second, inked sphere
  // on the same mesh out to the side (so the mesh has an edge table): the
  // window's far ink is still erased, and every lit pixel is one the scene
  // inks without the inkless sphere — it adds no contour. in the
  // line-drawing modes it draws no line
  // either: alone under TD_WIREFRAME it lands nothing, and under TD_SOLID
  // its fill plots no dark outline pixel.
  {
    TD_BoxMesh boxMesh;
    TD_SphereMesh sphereMesh;
    static uint8_t without[H][W];
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      TD_Object side(sphereMesh);
      setupK5(scene, box);
      scene.add(side);
      box.setPosition(0.0f, 0.0f, 0.9f);
      side.setScale(0.3f);
      side.setPosition(2.2f, 0.0f, 0.0f);
      renderToChecked(scene, target, TD_OUTLINE);
      std::memcpy(without, fb, sizeof fb);
    }
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      TD_Object side(sphereMesh);
      TD_Object sphere(sphereMesh);
      setupK5(scene, box);
      scene.add(side);
      scene.add(sphere);
      box.setPosition(0.0f, 0.0f, 0.9f);
      side.setScale(0.3f);
      side.setPosition(2.2f, 0.0f, 0.0f);
      sphere.setRotation(0.0f, 0.0f, 0.0f);
      sphere.setPosition(0.0f, 0.0f, -0.9f);
      sphere.setInk(false);
      CHECK(scene.prebuildOutline(), "prebuildOutline with an inkless object failed");
      renderToChecked(scene, target, TD_OUTLINE);
      long win = 0, extra = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) win += fb[y][x];
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) extra += fb[y][x] == 1 && without[y][x] != 1;
      CHECK(win == 0, "an inkless near fill must still erase the far ink (%ld/25 lit)", win);
      CHECK(extra == 0, "an inkless object must add no ink (%ld px)", extra);
    }
    {
      TD_Scene scene;
      TD_Object sphere(sphereMesh);
      setupK5(scene, sphere);
      sphere.setInk(false);
      renderToChecked(scene, target, TD_WIREFRAME);
      CHECK(stats().count == 0, "an inkless object under TD_WIREFRAME must land nothing (%d lit)",
            stats().count);
    }
    {
      TD_Scene scene;
      TD_Object sphere(sphereMesh);
      setupK5(scene, sphere);
      sphere.setInk(false);
      TD_Renderer &r = scene.renderer();
      r.setMode(TD_SOLID);
      TD_MonoTarget traced(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);
      traced.setAutoClear(false);     // only covered pixels are plotted
      std::memset(fb, 2, sizeof fb);  // 2 = never plotted: a dark plot is an outline
      CHECK(r.render(scene, scene.camera, traced), "solid render failed");
      long dark = 0, lit = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          dark += fb[y][x] == 0;
          lit += fb[y][x] == 1;
        }
      CHECK(lit > 0 && dark == 0, "an inkless object under TD_SOLID fills and plots no outline (%ld lit, %ld dark)",
            lit, dark);
    }
  }

  // ---- O6d. a line group draws one outline ----
  // parts sunk into each other, grouped, draw only their union's contour. a
  // small ball sunk into a big one, as a cheek sits on a head, both in one
  // group: near the big ball's silhouette, none of its ink lies where the
  // small one shows; turned in, where the small ball's rim floats over the
  // big one's face, none of the small ball's ink lies where the big one
  // shows (with all eight pixels round it showing too: at the union's
  // contour a grouped line reaches the background across a diagonal). by
  // default both ink through. a group of one draws exactly as no group,
  // alone and over a box behind it, so its contour stays closed; and an
  // object outside the group inks the same either way.
  {
    TD_SphereMesh bigMesh(24, 12, 1.0f), smallMesh(16, 8, 1.0f);
    static uint8_t shows[H][W], line[H][W], plain[H][W];
    const float cd = 2.0f - 0.3f * 1.2f;  // the small one's center 0.3 of its radius under the surface
    // the small ball at phi round from the view axis: whose ink, where the
    // other shows (that other's solid fill, every covered pixel lit; the
    // first one dark), with or without the group
    auto across = [&](float phi, bool bigInk, bool grouped) {
      TD_Scene scene;
      TD_Object big(bigMesh), small(smallMesh);
      scene.add(big);
      scene.add(small);
      setLegacyCamera(scene.camera);
      big.setScale(2.0f);
      small.setScale(1.2f);
      small.setPosition(cd * sinf(phi), 0.0f, -cd * cosf(phi));
      big.setInk(false);
      small.setInk(false);
      (bigInk ? big : small).setMaterial(TD_MATERIAL_BLACK);
      renderToChecked(scene, target, TD_SOLID);
      std::memcpy(shows, fb, sizeof fb);
      (bigInk ? big : small).setMaterial(TD_MATERIAL_LIT);
      (bigInk ? big : small).setInk(true);
      big.setInkGroup(grouped ? 1 : 0);
      small.setInkGroup(grouped ? 1 : 0);
      renderToChecked(scene, target, TD_OUTLINE);
      long n = 0;
      for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x) {
          bool all = fb[y][x] == 1;
          for (int dy = -1; dy <= 1 && all; ++dy)
            for (int dx = -1; dx <= 1 && all; ++dx) all = shows[y + dy][x + dx] != 0;
          n += all;
        }
      return n;
    };
    const long through = across(1.45f, true, false), throughGrouped = across(1.45f, true, true);
    CHECK(through > 5 && throughGrouped == 0,
          "grouped, the big ball's contour must not ink where the small one shows (%ld px; %ld ungrouped)",
          throughGrouped, through);
    const long rim = across(0.6f, false, false), rimGrouped = across(0.6f, false, true);
    CHECK(rim > 5 && rimGrouped == 0,
          "grouped, the small ball's rim must not ink where the big one shows (%ld px; %ld ungrouped)",
          rimGrouped, rim);

    TD_SphereMesh dense(48, 24), coarse(16, 8);
    TD_BoxMesh boxMesh;
    long differ = 0;
    for (int m = 0; m < 2; ++m)
      for (int k = 0; k < 6; ++k)
        for (int behind = 0; behind < 2; ++behind)
          for (int grouped = 0; grouped < 2; ++grouped) {
            TD_Scene scene;
            TD_Object ball(m ? coarse : dense), box(boxMesh);
            scene.add(ball);
            if (behind) scene.add(box);
            setLegacyCamera(scene.camera);
            ball.setScale(2.0f);
            ball.setRotation(0.2f * (float)k, 0.7f * (float)k, 0.0f);
            ball.setInkGroup(grouped ? 7 : 0);
            box.setScale(3.0f);
            box.setPosition(0.5f, 0.3f, 3.0f);
            box.setRotation(0.3f, 0.5f, 0.0f);
            renderToChecked(scene, target, TD_OUTLINE);
            if (!grouped) {
              std::memcpy(plain, fb, sizeof fb);
              continue;
            }
            for (int y = 0; y < H; ++y)
              for (int x = 0; x < W; ++x) differ += plain[y][x] != fb[y][x];
          }
    CHECK(differ == 0, "a group of one must draw exactly as no group (%ld px differ)", differ);

    for (int grouped = 0; grouped < 2; ++grouped) {
      TD_Scene scene;
      TD_Object big(bigMesh), small(smallMesh), box(boxMesh);
      scene.add(big);
      scene.add(small);
      scene.add(box);
      setLegacyCamera(scene.camera);
      big.setScale(2.0f);
      small.setScale(1.2f);
      small.setPosition(cd * sinf(1.45f), 0.0f, -cd * cosf(1.45f));
      box.setScale(1.5f);
      box.setPosition(1.4f, 0.6f, -1.5f);
      box.setRotation(0.4f, 0.6f, 0.0f);
      big.setInk(false);
      small.setInk(false);
      big.setInkGroup(grouped ? 1 : 0);
      small.setInkGroup(grouped ? 1 : 0);
      renderToChecked(scene, target, TD_OUTLINE);
      std::memcpy(grouped ? line : plain, fb, sizeof fb);
    }
    CHECK(stats().count > 20 && std::memcmp(plain, line, sizeof fb) == 0,
          "an object outside the group must ink the same either way (%ld px)", stats().count);
  }

  // ---- O6e. a fill-less object is glass ----
  // a box with its fill off, in the K5 pose: it fills nothing and records no
  // depth, so it hides nothing — a sphere inside it inks every pixel it inks
  // alone. under TD_OUTLINE its back faces' crease edges ink too, so all
  // twelve of its edges draw: every pixel the filled box inks, more beside,
  // and the middles of the three edges at its far corner, which the filled
  // box hides. its back faces' records count in lastDrawCount. a back face
  // inks no silhouette, so a glass sphere inks its contour alone. a filled
  // sphere between the eye and that corner hides the far edges inside its
  // disc and nowhere else. in the other modes it draws lines as the mode
  // draws them: under TD_SOLID and TD_CEL only its dark true edges, under
  // TD_SHADED nothing, under TD_WIREFRAME exactly the filled box's frame.
  // with its ink off too it plots nothing in any mode. prebuildOutline takes
  // it, and it renders beside a line group.
  {
    TD_BoxMesh boxMesh;
    TD_SphereMesh sphereMesh;
    const float bs = 1.4f;  // the box's scale
    static uint8_t filled[H][W], glass[H][W], alone[H][W], disc[H][W];
    TD_Vec3 farCorner = {0.0f, 0.0f, 0.0f};  // world space
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      setupK5(scene, box);
      box.setScale(bs);
      renderToChecked(scene, target, TD_OUTLINE);
      std::memcpy(filled, fb, sizeof fb);
      const long filledLit = stats().count;
      const uint32_t filledDrawn = scene.renderer().lastDrawCount();
      box.setFill(false);
      renderToChecked(scene, target, TD_OUTLINE);
      std::memcpy(glass, fb, sizeof fb);
      const long glassLit = stats().count;
      const uint32_t glassDrawn = scene.renderer().lastDrawCount();
      CHECK(filledDrawn == 3 && glassDrawn == 6,
            "a glass box's back faces must count as records (%u drawn; %u filled)",
            (unsigned)glassDrawn, (unsigned)filledDrawn);
      long missing = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) missing += filled[y][x] == 1 && glass[y][x] != 1;
      CHECK(missing == 0 && glassLit > filledLit,
            "a glass box must ink all the filled box inks and more (%ld px missing; %ld vs %ld lit)",
            missing, glassLit, filledLit);

      // the far corner is the box corner deepest in view; the camera is
      // resolved by the render above
      const TD_Camera &cam = scene.camera;
      const TD_Mat3 R = tdEulerXYZ(TILT, 0.7f, 0.0f);
      TD_Vec3 world[8], view[8];
      int fc = 0;
      for (int c = 0; c < 8; ++c) {
        const TD_Vec3 q = {(c & 1 ? 0.5f : -0.5f) * bs, (c & 2 ? 0.5f : -0.5f) * bs,
                           (c & 4 ? 0.5f : -0.5f) * bs};
        world[c] = tdMat3MulVec(R, q);
        view[c] = cam.worldToView(world[c]);
        if (view[c].z > view[fc].z) fc = c;
      }
      farCorner = world[fc];
      for (int b = 0; b < 3; ++b) {
        const TD_Vec3 &p = view[fc], &q = view[fc ^ (1 << b)];
        const TD_Vec3 m = {(p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f, (p.z + q.z) * 0.5f};
        const int mx = (int)floorf(cam.centerX() + cam.focalPixels() * m.x / m.z);
        const int my = (int)floorf(cam.centerY() - cam.focalPixels() * m.y / m.z);
        long lit = 0, litFilled = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int x = mx + dx, y = my + dy;
            if (x < 0 || x >= W || y < 0 || y >= H) continue;
            lit += glass[y][x] == 1;
            litFilled += filled[y][x] == 1;
          }
        CHECK(lit > 0 && litFilled == 0,
              "far edge %d's middle (%d,%d) must ink for the glass and not the filled box (%ld / %ld lit)",
              b, mx, my, lit, litFilled);
      }
    }

    // hides nothing: a sphere inside the glass inks every pixel it inks alone
    {
      TD_Scene scene;
      TD_Object ball(sphereMesh), box(boxMesh);
      setupK5(scene, ball);
      ball.setScale(0.8f);
      renderToChecked(scene, target, TD_OUTLINE);
      std::memcpy(alone, fb, sizeof fb);
      const long ballLit = stats().count;
      scene.add(box);
      box.setScale(bs);
      box.setRotation(TILT, 0.7f, 0.0f);
      box.setFill(false);
      renderToChecked(scene, target, TD_OUTLINE);
      long lost = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) lost += alone[y][x] == 1 && fb[y][x] != 1;
      CHECK(ballLit > 20 && lost == 0,
            "a glass must hide none of the sphere inside it (%ld of %ld px lost)", lost, ballLit);
    }

    // a back face inks only its crease and boundary edges: a sphere has
    // neither, so as glass it inks its contour ring alone, its inside clean
    {
      TD_Scene scene;
      TD_Object ball(sphereMesh);
      setupK5(scene, ball);
      ball.setRotation(0.0f, 0.0f, 0.0f);
      ball.setFill(false);
      renderToChecked(scene, target, TD_OUTLINE);
      const Stats s = stats();
      const long in = discCount((s.minx + s.maxx) * 0.5, (s.miny + s.maxy) * 0.5,
                                (s.maxx - s.minx) * 0.5 - 2.0);
      CHECK(s.count > 20 && in == 0, "a glass sphere must ink its contour alone (%ld lit inside)",
            in);
    }

    // is hidden by others: a filled sphere on the line from the eye to the
    // far corner, 0.7 short of it. its disc is its inkless solid footprint
    // alone; a far-edge pixel (inked by the glass, not by the filled box)
    // with all eight round it in the disc is hidden, one with none of the
    // 24 round it in the disc shows
    {
      const TD_Vec3 eye = {0.0f, 0.0f, -4.0f};
      const TD_Vec3 toEye = tdSub(eye, farCorner);
      const float len = tdLength(toEye);
      const float pull = 0.7f / len;
      TD_Scene scene;
      TD_Object box(boxMesh), ball(sphereMesh);
      setupK5(scene, box);
      box.setScale(bs);
      box.setFill(false);
      box.setVisible(false);
      scene.add(ball);
      ball.setScale(0.9f);
      ball.setPosition(farCorner.x + toEye.x * pull, farCorner.y + toEye.y * pull,
                       farCorner.z + toEye.z * pull);
      ball.setInk(false);
      renderToChecked(scene, target, TD_SOLID);
      std::memcpy(disc, fb, sizeof fb);
      box.setVisible(true);
      ball.setInk(true);
      renderToChecked(scene, target, TD_OUTLINE);
      long inside = 0, insideLit = 0, outside = 0, outsideDark = 0;
      for (int y = 2; y < H - 2; ++y)
        for (int x = 2; x < W - 2; ++x) {
          if (glass[y][x] != 1 || filled[y][x] == 1) continue;
          bool allIn = true, anyIn = false;
          for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
              const bool in = disc[y + dy][x + dx] == 1;
              anyIn = anyIn || in;
              if (dy >= -1 && dy <= 1 && dx >= -1 && dx <= 1) allIn = allIn && in;
            }
          if (allIn) {
            ++inside;
            insideLit += fb[y][x] == 1;
          } else if (!anyIn) {
            ++outside;
            outsideDark += fb[y][x] != 1;
          }
        }
      CHECK(inside > 5 && insideLit == 0,
            "the glass's far edges must hide inside the sphere's disc (%ld of %ld px lit)",
            insideLit, inside);
      CHECK(outside > 10 && outsideDark == 0,
            "the glass's far edges must show outside the sphere's disc (%ld of %ld px dark)",
            outsideDark, outside);
    }

    // the other modes, by the tracer: 2 is never plotted, a dark plot an
    // outline
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      setupK5(scene, box);
      box.setScale(bs);
      box.setFill(false);
      TD_Renderer &r = scene.renderer();
      TD_MonoTarget traced(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);
      traced.setAutoClear(false);  // only what is plotted lands
      const TD_RenderMode lineModes[2] = {TD_SOLID, TD_CEL};
      for (int i = 0; i < 2; ++i) {
        r.setMode(lineModes[i]);
        std::memset(fb, 2, sizeof fb);
        CHECK(r.render(scene, scene.camera, traced), "glass render failed in mode %d",
              (int)lineModes[i]);
        long dark = 0, lit = 0;
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) {
            dark += fb[y][x] == 0;
            lit += fb[y][x] == 1;
          }
        CHECK(lit == 0 && dark > 0,
              "a glass under mode %d must plot only its dark true edges (%ld lit, %ld dark)",
              (int)lineModes[i], lit, dark);
      }
      r.setMode(TD_SHADED);
      std::memset(fb, 2, sizeof fb);
      CHECK(r.render(scene, scene.camera, traced), "glass render failed under TD_SHADED");
      long plotted = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) plotted += fb[y][x] != 2;
      CHECK(plotted == 0, "a glass under TD_SHADED must plot nothing (%ld px)", plotted);

      static uint8_t wire[H][W];
      box.setFill(true);
      renderToChecked(scene, target, TD_WIREFRAME);
      std::memcpy(wire, fb, sizeof fb);
      box.setFill(false);
      renderToChecked(scene, target, TD_WIREFRAME);
      CHECK(stats().count > 0 && std::memcmp(wire, fb, sizeof fb) == 0,
            "a glass under TD_WIREFRAME must draw the filled box's frame (%ld px)",
            stats().count);
    }

    // both off: nothing plotted in any mode
    {
      TD_Scene scene;
      TD_Object box(boxMesh);
      setupK5(scene, box);
      box.setScale(bs);
      box.setFill(false);
      box.setInk(false);
      TD_Renderer &r = scene.renderer();
      TD_MonoTarget traced(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);
      traced.setAutoClear(false);
      long plotted = 0;
      for (int m = 0; m < TD_RENDER_MODE_COUNT; ++m) {
        r.setMode((TD_RenderMode)m);
        std::memset(fb, 2, sizeof fb);
        CHECK(r.render(scene, scene.camera, traced), "fill-less inkless render failed in mode %d",
              m);
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) plotted += fb[y][x] != 2;
      }
      CHECK(plotted == 0, "an object with fill and ink off must plot nothing (%ld px)", plotted);
    }

    // prebuild: a glass beside a line group
    {
      TD_Scene scene;
      TD_Object box(boxMesh), big(sphereMesh), small(sphereMesh);
      setupK5(scene, box);
      box.setScale(bs);
      box.setFill(false);
      scene.add(big);
      scene.add(small);
      big.setScale(0.8f);
      small.setScale(0.4f);
      small.setPosition(0.3f, 0.2f, -0.2f);
      big.setInkGroup(1);
      small.setInkGroup(1);
      CHECK(scene.prebuildOutline(), "prebuildOutline with a glass failed");
      renderToChecked(scene, target, TD_OUTLINE);
      CHECK(stats().count > 0, "a glass beside a line group must render ink");
    }
  }

  // ---- O6b. contour closure: the silhouette ring is one closed line ----
  // a dense sphere (facets under a pixel at this scale) at several poses: a
  // border flood of unlit pixels must never reach the rim center, so every
  // silhouette pixel survived the fills of the faces around it.
  {
    TD_SphereMesh dense(48, 24);
    TD_Scene scene;
    TD_Object sphere(dense);
    setupK5(scene, sphere);
    for (int k = 0; k < 5; ++k) {
      sphere.setRotation(0.2f * (float)k, 0.7f * (float)k, 0.0f);
      renderToChecked(scene, target, TD_OUTLINE);
      Stats s = stats();
      CHECK(s.count > 20, "pose %d: dense sphere contour suspiciously sparse (%ld px)", k,
            s.count);
      int cx = (s.minx + s.maxx) / 2, cy = (s.miny + s.maxy) / 2;
      CHECK(!floodReaches(cx, cy), "pose %d: contour ring is broken (border flood reached %d,%d)",
            k, cx, cy);
    }
  }

  // ---- O7. determinism: repeated outline renders match bit-exactly ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    static uint8_t fbFirst[H][W];
    renderToChecked(scene, target, TD_OUTLINE);
    CHECK(stats().count > 0, "outline determinism frame is empty");
    std::memcpy(fbFirst, fb, sizeof(fb));
    renderToChecked(scene, target, TD_OUTLINE);
    CHECK(std::memcmp(fbFirst, fb, sizeof(fb)) == 0,
          "identical outline renders must match bit-exactly");
  }

  // ---- O8. crease knob reaches pixels; prebuild/invalidate round-trip ----
  // a box shows its front-front vertical edge at the 60-degree default; a
  // crease threshold past 90 degrees un-inks it (the edge is not a
  // silhouette — both its faces front). prebuild must succeed up front,
  // invalidation must rebuild lazily to the identical frame.
  {
    TD_BoxMesh boxMesh;
    TD_Scene scene2;
    TD_Object obj(boxMesh);
    setupK5(scene2, obj);
    CHECK(scene2.prebuildOutline(), "prebuildOutline failed (status alloc?)");
    renderToChecked(scene2, target, TD_OUTLINE);
    uint64_t hDefault = fbHash();
    long litDefault = stats().count;
    scene2.setOutlineCrease(3.0f);  // ~172 degrees: nothing creases
    CHECK(scene2.outlineCrease() == 3.0f, "outlineCrease must round-trip");
    renderToChecked(scene2, target, TD_OUTLINE);
    uint64_t hLoose = fbHash();
    long litLoose = stats().count;
    CHECK(hLoose != hDefault, "crease threshold must reach pixels");
    CHECK(litLoose < litDefault,
          "loose crease must ink fewer pixels (%ld vs %ld)", litLoose, litDefault);
    scene2.setOutlineCrease(TD_OUTLINE_CREASE_DEFAULT);
    scene2.renderer().invalidateOutline();  // drop every table: prepare rebuilds
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hDefault, "invalidate + lazy rebuild must reproduce the frame");
  }

  // ---- O8b. a baked edge table stands in for the built one ----
  // the barrel's table, built and copied out the way tools/bake_edges.cpp
  // writes it, draws the frame the built table draws. a doctored copy (every
  // slot a crease) reaches pixels, so the ink pass reads the baked table; a
  // crease it wasn't baked at falls back to the built table, and the baked
  // crease picks it up again. the registration outlives invalidateOutline
  // and releaseBuffers, a second call replaces it, and a table that doesn't
  // fit the mesh is refused, leaving the registration as it was.
  {
    TD_Scene built;
    TD_Object builtObj(barrel_mesh);
    setupK5(built, builtObj);
    renderToChecked(built, target, TD_OUTLINE);
    const uint64_t hBuilt = fbHash();
    const long litBuilt = stats().count;
    built.setOutlineCrease(3.0f);
    renderToChecked(built, target, TD_OUTLINE);
    const uint64_t hLoose = fbHash();

    const uint16_t n = barrel_mesh.faceVertCount;
    TD_EdgeTable t;
    CHECK(t.build(barrel_mesh, TD_OUTLINE_CREASE_DEFAULT), "barrel edge table build failed");
    uint16_t *opp = (uint16_t *)std::malloc(n * sizeof(uint16_t));
    uint16_t *badOpp = (uint16_t *)std::malloc(n * sizeof(uint16_t));
    uint8_t *flags = (uint8_t *)std::malloc(n);
    uint8_t *allCrease = (uint8_t *)std::malloc(n);
    std::memcpy(opp, t.opp, n * sizeof(uint16_t));
    std::memcpy(badOpp, t.opp, n * sizeof(uint16_t));
    std::memcpy(flags, t.flags, n);
    std::memset(allCrease, TD_EDGE_CREASE, n);
    t.release();
    badOpp[n / 2] = barrel_mesh.nfaces;  // one past the last face
    const TD_MeshEdges baked = {opp, flags, barrel_mesh.nverts, barrel_mesh.nfaces, n,
                                TD_OUTLINE_CREASE_DEFAULT};
    const TD_MeshEdges doctored = {opp, allCrease, barrel_mesh.nverts, barrel_mesh.nfaces, n,
                                   TD_OUTLINE_CREASE_DEFAULT};
    const TD_MeshEdges outOfRange = {badOpp, flags, barrel_mesh.nverts, barrel_mesh.nfaces, n,
                                     TD_OUTLINE_CREASE_DEFAULT};
    const TD_MeshEdges otherCounts = {opp, flags, barrel_mesh.nverts, barrel_mesh.nfaces,
                                      (uint16_t)(n - 1), TD_OUTLINE_CREASE_DEFAULT};

    TD_Scene scene2;
    TD_Object obj(barrel_mesh);
    setupK5(scene2, obj);
    CHECK(scene2.setOutlineEdges(barrel_mesh, baked), "a matching baked table must be accepted");
    CHECK(scene2.prebuildOutline(), "prebuildOutline with a baked table failed");
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hBuilt, "the baked table must draw the built table's frame");

    CHECK(scene2.setOutlineEdges(barrel_mesh, doctored), "a second call must replace the table");
    renderToChecked(scene2, target, TD_OUTLINE);
    const uint64_t hDoctored = fbHash();
    CHECK(hDoctored != hBuilt && stats().count > litBuilt,
          "the ink pass must read the baked table (all-crease flags ink more: %ld vs %ld px)",
          stats().count, litBuilt);

    scene2.setOutlineCrease(3.0f);  // not the baked crease: the table builds
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hLoose, "another crease must fall back to the built table");
    scene2.setOutlineCrease(TD_OUTLINE_CREASE_DEFAULT);
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hDoctored, "the baked crease must pick the baked table up again");

    scene2.renderer().invalidateOutline();
    scene2.renderer().releaseBuffers();
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hDoctored, "the registration must outlive invalidateOutline and releaseBuffers");

    CHECK(!scene2.setOutlineEdges(barrel_mesh, otherCounts), "a table for other counts must be refused");
    CHECK(!scene2.setOutlineEdges(barrel_mesh, outOfRange), "an out-of-range opp must be refused");
    renderToChecked(scene2, target, TD_OUTLINE);
    CHECK(fbHash() == hDoctored, "a refused table must leave the registration as it was");

    std::free(opp);
    std::free(badOpp);
    std::free(flags);
    std::free(allCrease);
  }

  // ---- O9. opt-out identity: outline knobs must not perturb other modes ----
  // the K5 BAYER8 golden hash restated locally (GOLDEN_FRAME is block-scoped
  // inside runRenderTests); a deliberate regeneration there must update this
  // constant too.
  {
    static const uint64_t K5_BAYER8 = 0xf6f7fda5c9698e6eULL;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setOutlineCrease(0.5f);
    CHECK(scene.prebuildOutline(), "prebuildOutline failed on the K5 scene");
    renderToChecked(scene, target, TD_SHADED);
    uint64_t hsh = fbHash();
    CHECK(hsh == K5_BAYER8, "outline knobs leaked into TD_SHADED: 0x%llxULL != K5 0x%llxULL",
          (unsigned long long)hsh, (unsigned long long)K5_BAYER8);
  }

  // ---- eyeball frames (no checks; visual sanity in the log) ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    renderTo(scene, target, TD_OUTLINE);
    dump("outline: barrel, K5 pose");
    TD_SphereMesh sphereMesh;
    TD_ConeMesh coneMesh;
    TD_Scene scene2;
    TD_Object sphere(sphereMesh);
    TD_Object cone(coneMesh);
    setupK5(scene2, sphere);
    scene2.add(cone);
    sphere.setRotation(0.0f, 0.0f, 0.0f);
    cone.setScale(0.5f);
    cone.setRotation(-(float)M_PI / 2, 0.0f, 0.0f);
    cone.setPosition(0.0f, 0.0f, -0.8f);
    renderTo(scene2, target, TD_OUTLINE);
    dump("outline: cone poking out of sphere, front-on");
  }

  CHECK(oobCount == 0, "outline suite plotted out of bounds %ld times", oobCount);
  clearFb();
}
