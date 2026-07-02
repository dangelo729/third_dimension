// SPDX-License-Identifier: MIT
// render suite: geometric/lighting invariants, painter-sort behavior, golden
// frames, and fuzzing, driven through the scene API with the renderer
// exercised directly (TD_Renderer::render with a TD_Scene and its camera).
//
// everything single-object runs under the legacy mapping: identity view
// basis at eye (0,0,-4), object at the origin, scale 1 — the configuration
// that pins the golden frames bit-for-bit (M == R_obj elementwise,
// T == (0,0,4), view.z evaluated in a fixed expression order).

#include <cmath>
#include <cstring>

#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "TD_Scene.h"
#include <models/TD_Model_Barrel.h>

#include "test_common.h"

// demo pose tilt: top of the model tipped toward the camera.
static const float TILT = -0.30f;

static void setLegacyCamera(TD_Camera &cam) {
  cam.lookAt({0.0f, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f});
  cam.setFocalPixels(13.0f * sqrtf(16.0f - 1.0f));  // fit: FIT_PX * sqrt(D*D - 1)
  cam.setNearSkip(0.25f);
  cam.setCenterPixels(W * 0.5f, H * 0.5f);
}

// base fixture: legacy camera, elevated headlight, demo ambient.
static void setupBase(TD_Scene &scene) {
  setLegacyCamera(scene.camera);
  scene.light.setDirection(0.0f, 0.45f);
  scene.light.setIntensity(1.0f);
  scene.setAmbient(0.15f);
}

static bool renderScene(TD_Scene &scene, TD_MonoTarget &target, TD_RenderMode mode) {
  clearFb();
  TD_Renderer &r = scene.renderer();
  r.setMode(mode);
  return r.render(scene, scene.camera, target);
}

static void renderChecked(TD_Scene &scene, TD_MonoTarget &target, TD_RenderMode mode) {
  bool ok = renderScene(scene, target, mode);
  CHECK(ok, "render returned false (status %d)", (int)scene.lastStatus());
}

void runRenderTests() {
  TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);

  // ---- A. solid silhouette: centered, sane size, on-screen, at any yaw ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    for (int k = 0; k < 8; ++k) {
      float yaw = k * 0.4f;
      obj.setRotation(TILT, yaw, 0.0f);
      renderChecked(scene, target, TD_SOLID);
      Stats s = stats();
      double bx = (s.minx + s.maxx) * 0.5, by = (s.miny + s.maxy) * 0.5;
      CHECK(s.count > 150, "yaw %.1f: silhouette too small (%ld px)", yaw, s.count);
      CHECK(std::fabs(bx - 63.5) < 3.0, "yaw %.1f: bbox center x %.1f off-center", yaw, bx);
      CHECK(std::fabs(by - 15.5) < 3.0, "yaw %.1f: bbox center y %.1f off-center", yaw, by);
      int w = s.maxx - s.minx + 1, h = s.maxy - s.miny + 1;
      CHECK(w >= 16 && w <= 29, "yaw %.1f: width %d outside [16,29]", yaw, w);
      CHECK(h >= 16 && h <= 29, "yaw %.1f: height %d outside [16,29]", yaw, h);
      CHECK(s.miny > 0 && s.maxy < H - 1, "yaw %.1f: touches top/bottom edge", yaw);
    }
  }

  // ---- B. light response (shaded, fixed pose) ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    obj.setRotation(TILT, 0.7f, 0.0f);

    // true silhouette: shaded with shade floor 1.0 => every covered pixel on
    scene.setAmbient(1.0f);
    renderChecked(scene, target, TD_SHADED);
    long sil = stats().count;
    scene.setAmbient(0.15f);

    scene.light.setDirection(0.0f, 0.45f);  // headlight
    renderChecked(scene, target, TD_SHADED);
    long cFront = stats().count;

    scene.light.setDirection((float)M_PI, 0.45f);  // backlight
    renderChecked(scene, target, TD_SHADED);
    long cBack = stats().count;

    CHECK(cFront > 0 && cBack > 0, "lit pixel counts must be nonzero (%ld, %ld)", cFront, cBack);
    CHECK(cFront < sil, "dithered fill (%ld) should be sparser than solid (%ld)", cFront, sil);
    CHECK(cFront >= (long)(0.45 * sil), "headlight too dark: %ld vs silhouette %ld", cFront, sil);
    CHECK(cBack <= (long)(0.60 * sil), "backlight too bright: %ld vs silhouette %ld", cBack, sil);
    CHECK(cFront >= (long)(1.4 * cBack),
          "headlight (%ld) not clearly brighter than backlight (%ld)", cFront, cBack);

    scene.light.setDirection((float)M_PI / 2, 0.45f);  // light at -x => left side lit
    renderChecked(scene, target, TD_SHADED);
    double mxLeft = stats().cx;
    scene.light.setDirection(-(float)M_PI / 2, 0.45f);  // light at +x => right side lit
    renderChecked(scene, target, TD_SHADED);
    double mxRight = stats().cx;
    CHECK(mxLeft < mxRight - 1.0,
          "lit centroid should track the light: az=+pi/2 cx=%.1f, az=-pi/2 cx=%.1f",
          mxLeft, mxRight);
  }

  // ---- C. mode sanity ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    obj.setRotation(TILT, 0.7f, 0.0f);

    scene.setAmbient(1.0f);
    renderChecked(scene, target, TD_SHADED);
    long sil = stats().count;
    scene.setAmbient(0.15f);
    renderChecked(scene, target, TD_WIREFRAME);
    long cWire = stats().count;
    renderChecked(scene, target, TD_SHADED);
    long cDith = stats().count;
    renderChecked(scene, target, TD_SOLID);
    long cSolid = stats().count;
    CHECK(cWire >= 40, "wireframe suspiciously sparse (%ld px)", cWire);
    CHECK(cWire < cDith, "expected wire(%ld) < dithered(%ld)", cWire, cDith);
    CHECK(cSolid > cWire, "solid (%ld) should cover more than wireframe (%ld)", cSolid, cWire);
    CHECK(cSolid > (long)(0.5 * sil) && cSolid < sil,
          "solid (%ld) should be a mostly-filled silhouette (%ld)", cSolid, sil);
  }

  // ---- K5. golden frames through the full pipeline, every dither method ----
  // pins the ordered methods' rendered output bit-exactly (the tile
  // signatures alone only pin td_ditherOn, not the span loop's use of it —
  // a one-pixel pattern misalignment passes every density/count check), and
  // for FLOYD/ATKINS pins the kernels, serpentine scan, absorb rules and the
  // span->shade-buffer detour. distinct hashes per method also prove the
  // render path honors the target's dither state.
  // pinned golden hashes under the legacy mapping; regenerate them only
  // deliberately, never as a casual refresh.
  {
    static const uint64_t GOLDEN_FRAME[TD_DITHER_COUNT] = {
        0xf6f7fda5c9698e6eULL,  // BAYER8
        0xf8d8a394300721b2ULL,  // BAYER4
        0xf430250810ae4e34ULL,  // BAYER2
        0x50d744e482412e0bULL,  // BLUE16
        0x562853a6ae9977a0ULL,  // CLUST8
        0x362d14a670b639a4ULL,  // IGN
        0x3c6b0af40b58a13dULL,  // SCAN
        0x7bce5d22f01aa067ULL,  // MONO
        0x4a9992d1a9a2cf82ULL,  // FLOYD
        0xb742d6cecf8cd14bULL,  // ATKINS
    };
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    obj.setRotation(TILT, 0.7f, 0.0f);
    scene.light.setDirection(0.6f, 0.45f);  // oblique: a wide spread of mid shades
    for (int m = 0; m < TD_DITHER_COUNT; ++m) {
      const char *nm = td_ditherName((TD_Dither)m);
      CHECK(target.setDither((TD_Dither)m), "%s: setDither failed", nm);
      renderChecked(scene, target, TD_SHADED);
      CHECK(stats().count > 0, "%s: shaded render is empty", nm);
      uint64_t hsh = fbHash();
      CHECK(hsh == GOLDEN_FRAME[m], "%s: frame hash 0x%llxULL != pinned 0x%llxULL", nm,
            (unsigned long long)hsh, (unsigned long long)GOLDEN_FRAME[m]);
    }
    target.setDither(TD_DITHER_BAYER8);
  }

  // ---- D. fuzz: wild params must never crash or plot out of bounds ----
  for (int k = 0; k < 600; ++k) {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    float rx = frand(-10.0f, 10.0f);
    float ry = frand(-10.0f, 10.0f);
    float rz = frand(-10.0f, 10.0f);
    float az = frand(-10.0f, 10.0f);
    float el = frand(-10.0f, 10.0f);
    float dist = frand(0.2f, 9.0f);  // includes inside-the-model near-skip cases
    float focal = frand(1.0f, 500.0f);
    float cx = frand(-50.0f, 180.0f);
    float cy = frand(-50.0f, 80.0f);
    float ambient = frand(0.0f, 1.0f);
    float intensity = frand(0.0f, 2.0f);
    obj.setRotation(rx, ry, rz);
    scene.light.setDirection(az, el);
    scene.light.setIntensity(intensity);
    scene.setAmbient(ambient);
    scene.camera.lookAt({0.0f, 0.0f, -dist}, {0.0f, 0.0f, 0.0f});
    scene.camera.setFocalPixels(focal);
    scene.camera.setCenterPixels(cx, cy);
    scene.camera.setNearSkip(0.25f);
    target.setDither((TD_Dither)(k % (TD_DITHER_COUNT + 3)));  // includes out-of-range
    // may legitimately draw nothing (near-skip) or fail; must never plot OOB
    renderScene(scene, target, (TD_RenderMode)(k % TD_RENDER_MODE_COUNT));
  }
  CHECK(oobCount == 0, "engine plotted out of bounds %ld times", oobCount);
  target.setDither(TD_DITHER_BAYER8);

  // sane configs must always draw something
  for (int k = 0; k < 100; ++k) {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    float rx = frand(-3.2f, 3.2f);
    float ry = frand(-3.2f, 3.2f);
    float rz = frand(-3.2f, 3.2f);
    float az = frand(-3.2f, 3.2f);
    obj.setRotation(rx, ry, rz);
    scene.light.setDirection(az, 0.45f);
    renderChecked(scene, target, (TD_RenderMode)(k % TD_RENDER_MODE_COUNT));
    CHECK(stats().count > 50, "sane config %d drew almost nothing", k);
  }
  CHECK(oobCount == 0, "engine plotted out of bounds %ld times (sane configs)", oobCount);

  // ---- E. degenerate meshes: must not crash or plot out of bounds ----
  {
    // CCW as seen by the camera (looking +z): normal (0,0,-1), wound 0->2->1
    static const float dv[3][3] = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0, 0.7f, 0}};
    static const uint16_t dt[1][3] = {{0, 2, 1}};
    static const float dn[3][3] = {{0, 0, -1}, {0, 0, -1}, {0, 0, -1}};  // 3 corners, flat
    static const uint16_t df[1][4] = {{0, 1, 0, 3}};
    static const uint16_t dl[3] = {0, 2, 1};
    static const TD_Mesh one = {dv, dt, dn, df, dl, nullptr, 3, 1, 1, 3, 1.0f};
    {
      TD_Scene scene;
      TD_Object obj(one);
      scene.add(obj);
      setupBase(scene);
      obj.setRotation(TILT, 0.0f, 0.0f);
      renderScene(scene, target, TD_SHADED);
      CHECK(stats().count > 20, "single front-facing triangle should draw");
    }

    static const float zv[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};  // zero area
    static const TD_Mesh zero = {zv, dt, dn, df, dl, nullptr, 3, 1, 1, 3, 1.0f};
    {
      TD_Scene scene;
      TD_Object obj(zero);
      scene.add(obj);
      setupBase(scene);
      obj.setRotation(TILT, 0.0f, 0.0f);
      renderScene(scene, target, TD_SHADED);  // must simply not crash
      CHECK(oobCount == 0, "degenerate cases plotted out of bounds");
    }
  }

  // ---- G. non-planar (folded) faces: per-tri cull must not punch holes ----
  {
    // a "quad" whose two fan tris face opposite ways; tri 0 is back-facing,
    // tri 1 front-facing — the face must still draw (a face-level cull keyed
    // on tri 0 alone would drop it entirely)
    static const float bv[4][3] = {
        {-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}};
    static const uint16_t bt[2][3] = {{0, 1, 2}, {0, 3, 2}};
    static const float bn[6][3] = {{0, 0, 1},  {0, 0, 1},  {0, 0, 1},    // tri 0, flat
                                   {0, 0, -1}, {0, 0, -1}, {0, 0, -1}};  // tri 1, flat
    static const uint16_t bf[1][4] = {{0, 2, 0, 4}};
    static const uint16_t bl[4] = {0, 1, 2, 3};
    static const TD_Mesh bent = {bv, bt, bn, bf, bl, nullptr, 4, 2, 1, 4, 1.0f};
    {
      TD_Scene scene;
      TD_Object obj(bent);
      scene.add(obj);
      setupBase(scene);  // no tilt: the quad stays screen-parallel
      renderScene(scene, target, TD_SHADED);
      CHECK(stats().count > 15, "folded quad: front-facing half must draw (%ld px)",
            stats().count);
    }

    // and a purely back-facing triangle must still be culled
    static const uint16_t st[1][3] = {{0, 1, 2}};
    static const float sn[3][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    static const uint16_t sf[1][4] = {{0, 1, 0, 3}};
    static const uint16_t sl[3] = {0, 1, 2};
    static const TD_Mesh back = {bv, st, sn, sf, sl, nullptr, 4, 1, 1, 3, 1.0f};
    {
      TD_Scene scene;
      TD_Object obj(back);
      scene.add(obj);
      setupBase(scene);
      renderScene(scene, target, TD_SHADED);
      CHECK(stats().count == 0, "back-facing triangle must be culled (%ld px)", stats().count);
    }
  }

  // ---- H. painter sort: nearer face must win where faces overlap ----
  {
    // big bright quad far (z=+0.4, headlight-facing, shade~0.92), smaller
    // darker quad near (z~-0.4, tilted 1 rad => shade~0.56), both centered.
    // sampling the central box must show the near face's dither density
    // (Bayer-8: the density windows below are pinned to it); a reversed
    // painter sort shows the far face's instead.
    static const float pv[8][3] = {
        {-0.9f, -0.9f, 0.4f}, {-0.9f, 0.9f, 0.4f}, {0.9f, 0.9f, 0.4f}, {0.9f, -0.9f, 0.4f},
        {-0.270151f, -0.5f, 0.020736f}, {-0.270151f, 0.5f, 0.020736f},
        {0.270151f, 0.5f, -0.820736f}, {0.270151f, -0.5f, -0.820736f}};
    static const uint16_t pt[4][3] = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}};
    static const float pn[12][3] = {  // 3 identical corners per tri: flat faces
        {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
        {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
        {-0.841471f, 0, -0.540302f}, {-0.841471f, 0, -0.540302f}, {-0.841471f, 0, -0.540302f},
        {-0.841471f, 0, -0.540302f}, {-0.841471f, 0, -0.540302f}, {-0.841471f, 0, -0.540302f}};
    static const uint16_t pf[2][4] = {{0, 2, 0, 4}, {2, 2, 4, 4}};
    static const uint16_t pl[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    static const TD_Mesh farOnly = {pv, pt, pn, pf, pl, nullptr, 8, 2, 1, 4, 1.0f};
    long boxFar = 0;
    {
      TD_Scene scene;
      TD_Object obj(farOnly);
      scene.add(obj);
      setupBase(scene);  // no tilt
      renderScene(scene, target, TD_SHADED);
      for (int y = 13; y <= 19; ++y)
        for (int x = 61; x <= 67; ++x) boxFar += fb[y][x];
      CHECK(boxFar >= 40, "baseline: far bright face should nearly fill the box (%ld/49)",
            boxFar);
    }

    static const TD_Mesh both = {pv, pt, pn, pf, pl, nullptr, 8, 4, 2, 8, 1.0f};
    {
      TD_Scene scene;
      TD_Object obj(both);
      scene.add(obj);
      setupBase(scene);
      renderScene(scene, target, TD_SHADED);
      long box = 0;
      for (int y = 13; y <= 19; ++y)
        for (int x = 61; x <= 67; ++x) box += fb[y][x];
      CHECK(box >= 17 && box <= 38,
            "painter sort: central box should show the near darker face (%ld/49, far-only %ld/49)",
            box, boxFar);
    }
  }

  // ---- I. scene tilt: the top cap must be the visible one ----
  {
    // top octagon of the barrel fixture (face 16: {32, 6, 64, 8}), sliced
    // out of the shipped model through the TD_Mesh view fields
    static const uint16_t topf[1][4] = {{0, 6, 0, 8}};
    const TD_Mesh top = {barrel_mesh.verts,          &barrel_mesh.tris[32],
                         &barrel_mesh.cnorms[32 * 3], topf,
                         &barrel_mesh.faceVerts[64],  nullptr,
                         barrel_mesh.nverts,          6,
                         1,                           8,
                         1.0f};
    TD_Scene scene;
    TD_Object obj(top);
    scene.add(obj);
    setupBase(scene);
    obj.setRotation(TILT, 0.0f, 0.0f);
    renderScene(scene, target, TD_SHADED);
    CHECK(stats().count > 0, "top cap should be visible at the scene tilt");
    obj.setRotation(0.30f, 0.0f, 0.0f);  // inverted tilt would show the bottom instead
    renderScene(scene, target, TD_SHADED);
    CHECK(stats().count == 0, "top cap visible from below: TILT sign convention broke");
  }

  // ---- M. multi-object scenes (two instances of one mesh) ----
  {
    // M1. coincident instances: every face's depth ties exactly, so this is
    // the worst case for the painter sort. the (grp, z, obj, face)
    // tie-breaks make the frame a pure function of the scene — repeated
    // renders must match bit-exactly.
    TD_Scene scene;
    TD_Object a(barrel_mesh);
    TD_Object b(barrel_mesh);
    scene.add(a);
    scene.add(b);
    setupBase(scene);
    a.setRotation(TILT, 0.7f, 0.0f);
    b.setRotation(TILT, 0.7f, 0.0f);
    static uint8_t fbFirst[H][W];
    renderChecked(scene, target, TD_SHADED);
    CHECK(stats().count > 150, "two-instance scene suspiciously small (%ld px)", stats().count);
    std::memcpy(fbFirst, fb, sizeof(fb));
    renderChecked(scene, target, TD_SHADED);
    CHECK(std::memcmp(fbFirst, fb, sizeof(fb)) == 0,
          "two-instance scene must render deterministically across frames");
  }
  {
    // M2. painter across objects: the near object must win the overlap.
    // white far object (never drops pixels) behind a black near object
    // (never speckles): the central window must be entirely lit with the
    // far object alone and entirely dark once the near object covers it —
    // exact counts, because materials ride the dither endpoint guarantees.
    long litFar = 0;
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      scene.add(farObj);
      setupBase(scene);
      farObj.setRotation(TILT, 0.7f, 0.0f);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      renderChecked(scene, target, TD_SHADED);
      long boxFar = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) boxFar += fb[y][x];
      CHECK(boxFar == 25, "baseline: WHITE far object should fill the window (%ld/25)", boxFar);
      litFar = stats().count;
    }
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      TD_Object nearObj(barrel_mesh);
      scene.add(farObj);
      scene.add(nearObj);
      setupBase(scene);
      farObj.setRotation(TILT, 0.7f, 0.0f);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      nearObj.setRotation(TILT, 0.7f, 0.0f);
      nearObj.setPosition(0.0f, 0.0f, -0.9f);
      nearObj.setScale(0.5f);
      nearObj.setMaterial(TD_MATERIAL_BLACK);
      renderChecked(scene, target, TD_SHADED);
      long boxBoth = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) boxBoth += fb[y][x];
      CHECK(boxBoth == 0, "near BLACK object must win the overlap (%ld/25 lit)", boxBoth);
      long litBoth = stats().count;
      // the near object covers most of the far silhouette; what survives is a
      // ring of ~100 px (a broken painter erases it to ~0, a missing near
      // object leaves the full ~275 and trips the litFar check below).
      CHECK(litBoth > 60, "occluded scene should still show the far object (%ld px)", litBoth);
      CHECK(litBoth < litFar,
            "the near BLACK object must knock lit pixels out of the far silhouette (%ld vs %ld)",
            litBoth, litFar);
    }
  }
  CHECK(oobCount == 0, "render suite plotted out of bounds %ld times", oobCount);

  // ---- F. eyeball frames (no checks; visual sanity in the log) ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    scene.add(obj);
    setupBase(scene);
    obj.setRotation(TILT, 0.7f, 0.0f);
    const char *names[4] = {
        "shaded, light az=0 (headlight)", "shaded, az=pi/2 (light from left)",
        "shaded, az=pi (backlight)", "shaded, az=3pi/2 (light from right)"};
    for (int k = 0; k < 4; ++k) {
      scene.light.setDirection(k * (float)M_PI / 2, 0.45f);
      renderScene(scene, target, TD_SHADED);
      dump(names[k]);
    }
    scene.light.setDirection(0.0f, 0.45f);
    renderScene(scene, target, TD_SOLID);
    dump("solid + facet edges");
    renderScene(scene, target, TD_WIREFRAME);
    dump("wireframe (front faces only)");
  }

  clearFb();
}
