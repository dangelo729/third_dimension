// SPDX-License-Identifier: MIT
// light suite: the multi-light surface. addLight/removeLight list mechanics,
// the added-light shade term (a relocation identity against the K5 pin plus
// its own two-light golden frames), saturation and fill-response invariants,
// unlit-path isolation, cel interaction, and wild-light fuzzing.

#include <cmath>
#include <cstring>

#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "TD_Scene.h"
#include <models/TD_Model_Barrel.h>

#include "test_common.h"

// the K5 golden barrel fixture from test_render.cpp, reproduced exactly:
// fixed camera, oblique light, demo ambient, demo pose. the L1/L4 hash
// identities depend on every value below.
static const float TILT = -0.30f;

static void setFixedCamera(TD_Camera &cam) {
  cam.lookAt({0.0f, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f});
  cam.setFocalPixels(13.0f * sqrtf(16.0f - 1.0f));  // a unit sphere at 4 fits a 13 px radius
  cam.setNearSkip(0.25f);
  cam.setCenterPixels(W * 0.5f, H * 0.5f);
}

static void setupK5(TD_Scene &scene, TD_Object &obj) {
  scene.add(obj);
  setFixedCamera(scene.camera);
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

// records seam traffic for L8: per-pixel write counts plus violations of the
// cel span contract — every span slope bitwise +0.0f, every span left shade
// bitwise on the allowed level/sentinel set, per-pixel plots only at the
// 0.0/2.0 ink sentinels.
struct LightSeamRecorder : TD_RenderTarget {
  uint16_t writes[H][W];
  long spanCalls = 0, plotCalls = 0;
  int badSlope = 0, badLevel = 0, badPlot = 0, oob = 0;
  float allowed[TD_CEL_MAX_BANDS + 2];
  int nAllowed = 0;
  bool checkCel = false;

  void reset(bool cel) {
    std::memset(writes, 0, sizeof(writes));
    spanCalls = plotCalls = 0;
    badSlope = badLevel = badPlot = oob = 0;
    nAllowed = 0;
    checkCel = cel;
  }

  int16_t width() override { return W; }
  int16_t height() override { return H; }

  void plotShade(int16_t x, int16_t y, float shade) override {
    ++plotCalls;
    if (x < 0 || x >= W || y < 0 || y >= H) {
      ++oob;
      return;
    }
    ++writes[y][x];
    if (checkCel && !(sameBits(shade, 0.0f) || sameBits(shade, 2.0f))) ++badPlot;
  }

  void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX, float leftShade,
                     float shadeSlope) override {
    (void)leftEdgeX;
    ++spanCalls;
    if (y < 0 || y >= H || xs < 0 || xe >= W || xs > xe) {
      ++oob;
      return;
    }
    if (checkCel) {
      if (!sameBits(shadeSlope, 0.0f)) ++badSlope;
      bool ok = false;
      for (int i = 0; i < nAllowed && !ok; ++i) ok = sameBits(leftShade, allowed[i]);
      if (!ok) ++badLevel;
    }
    for (int x = xs; x <= xe; ++x) ++writes[y][x];
  }
};

void runLightTests() {
  TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);

  // the K5 BAYER8 golden hash restated locally (GOLDEN_FRAME is block-scoped
  // inside runRenderTests); a deliberate regeneration there must update this
  // constant too.
  static const uint64_t K5_BAYER8 = 0xf6f7fda5c9698e6eULL;

  // pinned two-light golden hashes (the K5 scene plus one fill at az -0.9,
  // el 0.2, intensity 0.5, BAYER8) — the shaded sum shape and its coupling
  // into the cel quantizer. regenerate only deliberately.
  static const uint64_t GOLDEN_TWO_LIGHT = 0x8ec906b19ccd07e8ULL;
  static const uint64_t GOLDEN_TWO_LIGHT_CEL = 0x7f812396f3900a9fULL;

  // ---- L1. relocation identity: an added light IS the pinned key term ----
  // the K5 frame reproduced with the key light silenced and an added light
  // in its place. per corner the sum is (ambient + ((1-a)*diff)*0.0f) +
  // ((1-a)*d)*1.0f against the pin's ambient + ((1-a)*diff)*1.0f; they match
  // bitwise because the key term at intensity 0 is +/-0.0f (a -0.0f dot
  // product survives the < 0.0f clamp), nonzero ambient absorbs +/-0.0f
  // bitwise, and *1.0f is a bitwise identity. the premises are frozen
  // executably first, so a toolchain that breaks one fails loudly here, not
  // at the frame hash. this couples the added-light expression shape to the
  // K5 pin without minting a new one.
  {
    static const float reps[6] = {0.0f, -0.0f, 6.1e-5f, 0.2914f, 0.7f, 1.0f};
    for (int i = 0; i < 6; ++i) {
      float d = reps[i];
      float keyZero = (1.0f - 0.15f) * d * 0.0f;
      CHECK(keyZero == 0.0f, "L1 premise: key term at intensity 0 must be a zero (d=%.6g)", d);
      CHECK(sameBits(0.15f + keyZero, 0.15f),
            "L1 premise: ambient + +/-0.0f must be bitwise ambient (d=%.6g)", d);
      float fillTerm = (1.0f - 0.15f) * d * 1.0f;
      CHECK(sameBits(fillTerm, (1.0f - 0.15f) * d),
            "L1 premise: *1.0f must be a bitwise identity (d=%.6g)", d);
    }
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.light.setIntensity(0.0f);
    TD_Light moved;
    moved.setDirection(0.6f, 0.45f);  // the K5 key light, relocated
    moved.setIntensity(1.0f);
    scene.addLight(moved);
    renderToChecked(scene, target, TD_SHADED);
    uint64_t hsh = fbHash();
    CHECK(hsh == K5_BAYER8, "relocated light must reproduce the K5 pin: 0x%llxULL != 0x%llxULL",
          (unsigned long long)hsh, (unsigned long long)K5_BAYER8);
  }

  // ---- L2. two-light golden frame ----
  // freezes the summed shade shape and the key-first-then-add-order sum.
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    TD_Light fill;
    fill.setDirection(-0.9f, 0.2f);
    fill.setIntensity(0.5f);
    scene.addLight(fill);
    renderToChecked(scene, target, TD_SHADED);
    CHECK(stats().count > 0, "two-light shaded render is empty");
    uint64_t hsh = fbHash();
    CHECK(hsh == GOLDEN_TWO_LIGHT, "two-light frame hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)hsh, (unsigned long long)GOLDEN_TWO_LIGHT);
    CHECK(hsh != K5_BAYER8, "the added fill must reach pixels");
  }

  // ---- L3. two-light cel golden frame ----
  // the same scene through the band quantizer (defaults: 4 bands; ink NONE
  // so the pin is pure quantized fills) — freezes the summed-shade-into-
  // celFillSpan coupling.
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setCelInk(TD_CEL_INK_NONE);
    TD_Light fill;
    fill.setDirection(-0.9f, 0.2f);
    fill.setIntensity(0.5f);
    scene.addLight(fill);
    renderToChecked(scene, target, TD_CEL);
    CHECK(stats().count > 0, "two-light cel render is empty");
    uint64_t hsh = fbHash();
    CHECK(hsh == GOLDEN_TWO_LIGHT_CEL, "two-light cel hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)hsh, (unsigned long long)GOLDEN_TWO_LIGHT_CEL);
  }

  // ---- L4. list mechanics + determinism ----
  // membership operations land exactly on the documented contract: repeated
  // renders bit-identical, remove/re-add reproduces the pin, double add and
  // stray remove are no-ops, the key light is rejected, and add order is the
  // iteration order (probed destructively head-by-head — next_ is private).
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    TD_Light fill;
    fill.setDirection(-0.9f, 0.2f);
    fill.setIntensity(0.5f);
    scene.addLight(fill);

    static uint8_t fbFirst[H][W];
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == GOLDEN_TWO_LIGHT, "L4 baseline must be the pinned two-light frame");
    std::memcpy(fbFirst, fb, sizeof(fb));
    renderToChecked(scene, target, TD_SHADED);
    CHECK(std::memcmp(fbFirst, fb, sizeof(fb)) == 0,
          "identical two-light renders must match bit-exactly");

    scene.removeLight(fill);
    CHECK(scene.firstExtraLight() == nullptr, "removeLight must empty the one-light list");
    scene.addLight(fill);
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == GOLDEN_TWO_LIGHT, "remove + re-add must reproduce the pinned frame");

    scene.addLight(fill);  // already linked: no-op
    CHECK(scene.firstExtraLight() == &fill, "double addLight must leave the head alone");
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == GOLDEN_TWO_LIGHT, "double addLight must not change the frame");

    TD_Light stray;
    scene.removeLight(stray);  // never linked: no-op
    CHECK(scene.firstExtraLight() == &fill, "stray removeLight must leave the list alone");
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == GOLDEN_TWO_LIGHT, "stray removeLight must not change the frame");

    scene.removeLight(fill);
    scene.addLight(scene.light);  // the key light: rejected
    CHECK(scene.firstExtraLight() == nullptr, "the key light must never join the list");
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == K5_BAYER8, "addLight(scene.light) must leave the key-only K5 frame");

    // add order is iteration order, probed through the public head only.
    TD_Light a, b;
    a.setDirection(-0.9f, 0.2f);
    a.setIntensity(0.5f);
    b.setDirection(2.0f, -0.3f);
    b.setIntensity(0.25f);
    scene.addLight(a);
    scene.addLight(b);
    renderToChecked(scene, target, TD_SHADED);
    uint64_t hab = fbHash();
    CHECK(hab != GOLDEN_TWO_LIGHT, "the second added light must reach pixels");
    CHECK(scene.firstExtraLight() == &a, "head must be the first-added light");
    scene.removeLight(a);
    CHECK(scene.firstExtraLight() == &b, "after removing the head, the second light leads");
    scene.removeLight(b);
    CHECK(scene.firstExtraLight() == nullptr, "removing both must empty the list");
    scene.addLight(a);
    scene.addLight(b);
    renderToChecked(scene, target, TD_SHADED);
    CHECK(fbHash() == hab, "re-adding in the same order must reproduce the frame");
  }

  // ---- L5. saturation identity ----
  // six axis-aligned added lights at intensity 10: any unit normal has a
  // component >= 1/sqrt(3), so some rig light alone pushes every lit corner
  // past the clamp and every corner shade arrives as exactly 1.0f — the
  // frame must equal the ambient-1.0 flat-lit frame bit-for-bit (there every
  // corner is 1.0f + +/-0.0f, also exactly 1.0f).
  {
    CHECK((1.0f - 0.15f) * 0.577f * 10.0f > 1.0f, "L5 premise: one rig term must saturate");
    CHECK(sameBits(1.0f + (1.0f - 1.0f) * 0.5f * 1.0f, 1.0f),
          "L5 premise: flat-lit shade must be bitwise 1.0f");
    CHECK(sameBits(1.0f + (1.0f - 1.0f) * -0.0f * 1.0f, 1.0f),
          "L5 premise: flat-lit shade must absorb a -0.0f dot bitwise");
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setAmbient(1.0f);
    renderToChecked(scene, target, TD_SHADED);
    static uint8_t fbSil[H][W];
    std::memcpy(fbSil, fb, sizeof(fb));
    scene.setAmbient(0.15f);
    TD_Light rig[6];
    rig[0].setDirectionVector(1.0f, 0.0f, 0.0f);
    rig[1].setDirectionVector(-1.0f, 0.0f, 0.0f);
    rig[2].setDirectionVector(0.0f, 1.0f, 0.0f);
    rig[3].setDirectionVector(0.0f, -1.0f, 0.0f);
    rig[4].setDirectionVector(0.0f, 0.0f, 1.0f);
    rig[5].setDirectionVector(0.0f, 0.0f, -1.0f);
    for (int i = 0; i < 6; ++i) {
      rig[i].setIntensity(10.0f);
      scene.addLight(rig[i]);
    }
    renderToChecked(scene, target, TD_SHADED);
    CHECK(std::memcmp(fbSil, fb, sizeof(fb)) == 0,
          "a saturating rig must reproduce the ambient-1.0 silhouette bit-exactly");
  }

  // ---- L6. fill-light response ----
  // key from +x, half-intensity fill from -x: the fill pulls the lit
  // centroid back toward center, the lit count lands strictly between
  // key-only and the silhouette, and adding lights never darkens the frame
  // (ordered dither is monotone in shade per pixel).
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setAmbient(1.0f);
    renderToChecked(scene, target, TD_SHADED);
    long sil = stats().count;
    scene.setAmbient(0.15f);
    scene.light.setDirection(-(float)M_PI / 2, 0.45f);  // key from +x: right side lit
    renderToChecked(scene, target, TD_SHADED);
    Stats keyOnly = stats();
    TD_Light fillLight;
    fillLight.setDirection((float)M_PI / 2, 0.45f);  // fill from -x
    fillLight.setIntensity(0.5f);
    scene.addLight(fillLight);
    renderToChecked(scene, target, TD_SHADED);
    Stats both = stats();
    CHECK(both.cx < keyOnly.cx - 0.5,
          "fill must pull the lit centroid toward center (key-only cx=%.1f, both cx=%.1f)",
          keyOnly.cx, both.cx);
    CHECK(both.count > keyOnly.count,
          "adding a fill must never darken: key-only %ld px, both %ld px", keyOnly.count,
          both.count);
    CHECK(both.count < sil, "a half fill must stay short of the silhouette (%ld vs %ld)",
          both.count, sil);
    TD_Light rim;
    rim.setDirection((float)M_PI, 0.3f);
    rim.setIntensity(0.4f);
    scene.addLight(rim);
    renderToChecked(scene, target, TD_SHADED);
    long c3 = stats().count;
    CHECK(c3 >= both.count, "adding a rim must never decrease the lit count (%ld vs %ld)", c3,
          both.count);
    CHECK(c3 <= sil, "lit count can never exceed the silhouette (%ld vs %ld)", c3, sil);
  }

  // ---- L7. unlit paths never read lights ----
  // TD_SOLID and TD_WIREFRAME frames must be bit-identical with and without
  // a rig, and the M2 BLACK/WHITE overlap windows keep their exact counts
  // under one — materials branch away before the lighting loop.
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    renderToChecked(scene, target, TD_SOLID);
    uint64_t hSolid = fbHash();
    renderToChecked(scene, target, TD_WIREFRAME);
    uint64_t hWire = fbHash();
    TD_Light rig[3];
    rig[0].setDirection(1.0f, -0.5f);
    rig[0].setIntensity(2.0f);
    rig[1].setDirection(-2.0f, 0.8f);
    rig[1].setIntensity(0.7f);
    rig[2].setDirectionVector(0.0f, 1.0f, 0.0f);
    rig[2].setIntensity(5.0f);
    for (int i = 0; i < 3; ++i) scene.addLight(rig[i]);
    renderToChecked(scene, target, TD_SOLID);
    CHECK(fbHash() == hSolid, "TD_SOLID must ignore added lights");
    renderToChecked(scene, target, TD_WIREFRAME);
    CHECK(fbHash() == hWire, "TD_WIREFRAME must ignore added lights");
  }
  {
    // the M2 overlap fixture under a rig: WHITE far object fills the window
    // exactly, a near BLACK object empties it exactly. each scene gets its
    // own rig — a light stays linked when its scene dies, so reuse without
    // removeLight would silently no-op.
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      setupK5(scene, farObj);
      TD_Light rig[3];
      rig[0].setDirection(1.0f, -0.5f);
      rig[0].setIntensity(2.0f);
      rig[1].setDirection(-2.0f, 0.8f);
      rig[1].setIntensity(0.7f);
      rig[2].setDirectionVector(0.0f, 1.0f, 0.0f);
      rig[2].setIntensity(5.0f);
      for (int i = 0; i < 3; ++i) scene.addLight(rig[i]);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      renderToChecked(scene, target, TD_SHADED);
      long box = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) box += fb[y][x];
      CHECK(box == 25, "WHITE far object must fill the window under a rig (%ld/25)", box);
    }
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      TD_Object nearObj(barrel_mesh);
      setupK5(scene, farObj);
      scene.add(nearObj);
      TD_Light rig[3];
      rig[0].setDirection(1.0f, -0.5f);
      rig[0].setIntensity(2.0f);
      rig[1].setDirection(-2.0f, 0.8f);
      rig[1].setIntensity(0.7f);
      rig[2].setDirectionVector(0.0f, 1.0f, 0.0f);
      rig[2].setIntensity(5.0f);
      for (int i = 0; i < 3; ++i) scene.addLight(rig[i]);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      nearObj.setRotation(TILT, 0.7f, 0.0f);
      nearObj.setPosition(0.0f, 0.0f, -0.9f);
      nearObj.setScale(0.5f);
      nearObj.setMaterial(TD_MATERIAL_BLACK);
      renderToChecked(scene, target, TD_SHADED);
      long box = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) box += fb[y][x];
      CHECK(box == 0, "near BLACK object must win the overlap under a rig (%ld/25 lit)", box);
    }
  }

  // ---- L8. cel seam + partition under a rig ----
  // two-light cel traffic honors the span contract (flat sub-spans, exact
  // band levels, ink at the sentinels); at three lights, ink-NONE TD_CEL
  // writes exactly the pixels TD_SHADED writes, exactly as often.
  {
    static LightSeamRecorder rec;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    TD_Light fill;
    fill.setDirection(-0.9f, 0.2f);
    fill.setIntensity(0.5f);
    scene.addLight(fill);
    rec.reset(true);
    for (int b = 0; b < 4; ++b) rec.allowed[rec.nAllowed++] = td_celLevel(b, 0.15f, 4);
    rec.allowed[rec.nAllowed++] = 0.0f;
    rec.allowed[rec.nAllowed++] = 2.0f;
    CHECK(renderTo(scene, rec, TD_CEL), "two-light seam render failed (status %d)",
          (int)scene.lastStatus());
    CHECK(rec.spanCalls > 0, "two-light seam audit saw no spans");
    CHECK(rec.plotCalls > 0, "two-light black ink must reach the per-pixel path");
    CHECK(rec.badSlope == 0, "%d two-light spans with a non-+0.0f slope", rec.badSlope);
    CHECK(rec.badLevel == 0, "%d two-light spans off the band-level set", rec.badLevel);
    CHECK(rec.badPlot == 0, "%d two-light plots outside the 0.0/2.0 ink sentinels", rec.badPlot);
    CHECK(rec.oob == 0, "%d two-light seam calls out of bounds", rec.oob);
  }
  {
    static LightSeamRecorder rec;
    static uint16_t shadedWrites[H][W];
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setCelInk(TD_CEL_INK_NONE);
    TD_Light rig[3];
    rig[0].setDirection(-0.9f, 0.2f);
    rig[0].setIntensity(0.5f);
    rig[1].setDirection(2.4f, 0.1f);
    rig[1].setIntensity(0.35f);
    rig[2].setDirectionVector(0.0f, 1.0f, 0.0f);
    rig[2].setIntensity(0.6f);
    for (int i = 0; i < 3; ++i) scene.addLight(rig[i]);
    rec.reset(false);
    CHECK(renderTo(scene, rec, TD_SHADED), "three-light partition baseline render failed");
    std::memcpy(shadedWrites, rec.writes, sizeof(shadedWrites));
    rec.reset(false);
    CHECK(renderTo(scene, rec, TD_CEL), "three-light partition cel render failed");
    CHECK(std::memcmp(shadedWrites, rec.writes, sizeof(shadedWrites)) == 0,
          "three-light cel sub-spans must cover exactly the shaded span pixels, exactly as often");
  }

  // ---- L9. wild-light fuzz ----
  // 0..4 added lights with wild directions and intensities in [-2, 2] (a
  // negative intensity legitimately pulls shades below ambient), every mode,
  // every dither: frames must render and never plot out of bounds. the LCG
  // stream is saved and restored so later suites' fuzz inputs stay put.
  {
    uint32_t saved = lcg;
    for (int k = 0; k < 200; ++k) {
      TD_Scene scene;
      TD_Object obj(barrel_mesh);
      setupK5(scene, obj);
      obj.setRotation(frand(-10.0f, 10.0f), frand(-10.0f, 10.0f), frand(-10.0f, 10.0f));
      scene.light.setDirection(frand(-10.0f, 10.0f), frand(-10.0f, 10.0f));
      scene.light.setIntensity(frand(-2.0f, 2.0f));
      scene.setAmbient(frand(0.0f, 1.0f));
      TD_Light extras[4];
      int n = k % 5;
      for (int i = 0; i < n; ++i) {
        extras[i].setDirection(frand(-10.0f, 10.0f), frand(-10.0f, 10.0f));
        extras[i].setIntensity(frand(-2.0f, 2.0f));
        scene.addLight(extras[i]);
      }
      target.setDither((TD_Dither)(k % TD_DITHER_COUNT));
      bool ok = renderTo(scene, target, (TD_RenderMode)(k % TD_RENDER_MODE_COUNT));
      CHECK(ok, "wild-light fuzz render %d failed (status %d)", k, (int)scene.lastStatus());
    }
    lcg = saved;
    target.setDither(TD_DITHER_BAYER8);
    CHECK(oobCount == 0, "wild-light fuzz plotted out of bounds %ld times", oobCount);
  }

  CHECK(oobCount == 0, "light suite plotted out of bounds %ld times", oobCount);
  clearFb();
}
