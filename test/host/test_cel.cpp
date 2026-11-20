// SPDX-License-Identifier: MIT
// cel suite: pins the TD_CEL quantizer math (band index, band level, band
// threshold — the exact expressions the renderer's span splitter calls) and
// the banded render path itself: golden frames, seam traffic, sentinel
// bypass, and the coverage identity against TD_SHADED.

#include <cmath>
#include <cstring>

#include "TD_Color565Target.h"
#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "TD_Scene.h"
#include <models/TD_Model_Barrel.h>

#include "test_common.h"

// the K5 golden barrel fixture from test_render.cpp, reproduced exactly:
// fixed camera, oblique light, demo ambient, demo pose. Q5 and Q12 hash
// equality depends on every value below.
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

// records seam traffic for the audit sections: per-pixel write counts plus
// violations of the cel span contract — every span slope bitwise +0.0f,
// every span left shade bitwise on the allowed level/sentinel set, per-pixel
// plots only at the 0.0/2.0 ink sentinels.
struct CelSeamRecorder : TD_RenderTarget {
  uint16_t writes[H][W];
  long spanCalls = 0, plotCalls = 0;
  int badSlope = 0, badLevel = 0, badPlot = 0, oob = 0;
  float allowed[TD_CEL_MAX_BANDS + 2];
  int nAllowed = 0;
  bool checkCel = false;

  static bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

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

void runCelTests() {
  TD_MonoTarget target(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);

  // ---- Q1. quantizer endpoints (bitwise) ----
  // band 0 is exactly the ambient floor, the top band exactly 1.0f; the
  // floor classifies into band 0 and full brightness into the top band.
  {
    static const float ambients[4] = {0.0f, 0.15f, 0.5f, 0.9f};
    for (int n = 2; n <= TD_CEL_MAX_BANDS; ++n) {
      for (int ai = 0; ai < 4; ++ai) {
        float a = ambients[ai];
        uint8_t nb = (uint8_t)n;
        CHECK(td_celLevel(0, a, nb) == a,
              "bands %d ambient %.2f: bottom level %.9g != ambient", n, a,
              td_celLevel(0, a, nb));
        CHECK(td_celLevel(n - 1, a, nb) == 1.0f,
              "bands %d ambient %.2f: top level %.9g != 1.0f", n, a,
              td_celLevel(n - 1, a, nb));
        CHECK(td_celBand(a, a, nb) == 0,
              "bands %d ambient %.2f: ambient shade must classify to band 0", n, a);
        CHECK(td_celBand(1.0f, a, nb) == n - 1,
              "bands %d ambient %.2f: shade 1.0 must classify to the top band", n, a);
      }
    }
  }

  // ---- Q2. monotonicity + threshold consistency ----
  // band index nondecreasing over a shade sweep, levels strictly increasing
  // over bands, and classification agreeing with the threshold values an ulp
  // either side (structural today — td_celBand compares against td_celThresh
  // itself — frozen here against future refactors).
  {
    static const float ambients[3] = {0.0f, 0.15f, 0.5f};
    for (int n = 2; n <= TD_CEL_MAX_BANDS; ++n) {
      for (int ai = 0; ai < 3; ++ai) {
        float a = ambients[ai];
        uint8_t nb = (uint8_t)n;
        int prev = 0;
        for (int k = 0; k <= 512; ++k) {
          int b = td_celBand((float)k / 512.0f, a, nb);
          CHECK(b >= prev && b <= n - 1,
                "bands %d ambient %.2f: band %d not monotone at shade %d/512", n, a, b, k);
          prev = b;
        }
        for (int b = 1; b < n; ++b)
          CHECK(td_celLevel(b, a, nb) > td_celLevel(b - 1, a, nb),
                "bands %d ambient %.2f: level %d not above level %d", n, a, b, b - 1);
        for (int k = 0; k <= n - 2; ++k) {
          float thr = td_celThresh(k, a, nb);
          CHECK(td_celBand(thr, a, nb) == k,
                "bands %d ambient %.2f: shade at threshold %d must stay in band %d", n, a, k, k);
          CHECK(td_celBand(std::nextafterf(thr, 0.0f), a, nb) == k,
                "bands %d ambient %.2f: an ulp below threshold %d must be band %d", n, a, k, k);
          CHECK(td_celBand(std::nextafterf(thr, 2.0f), a, nb) == k + 1,
                "bands %d ambient %.2f: an ulp above threshold %d must be band %d", n, a, k,
                k + 1);
        }
      }
    }
  }

  // ---- Q3. degeneracy: non-finite shades, setter clamps ----
  // NaN falls through every strict > comparison to band 0; +/-inf classify
  // to the extreme bands; band-count setters clamp instead of asserting.
  // documented, not asserted: at ambient within a few ulps of 1.0 a
  // threshold can round to exactly 1.0f, making the top band unreachable —
  // a harmless degradation. Q1's endpoint identities are claimed over its
  // tested ambient set only.
  {
    CHECK(td_celBand(std::nanf(""), 0.15f, 4) == 0, "NaN shade must map to band 0");
    CHECK(td_celBand(-INFINITY, 0.15f, 4) == 0, "-inf shade must map to band 0");
    CHECK(td_celBand(INFINITY, 0.15f, 4) == 3, "+inf shade must map to the top band");
    TD_Scene scene;
    CHECK(scene.celBands() == 4, "default band count must be 4");
    CHECK(scene.celInk() == TD_CEL_INK_BLACK, "default ink must be black");
    scene.setCelBands(0);
    CHECK(scene.celBands() == 2, "bands 0 must clamp to 2 (got %d)", scene.celBands());
    scene.setCelBands(1);
    CHECK(scene.celBands() == 2, "bands 1 must clamp to 2 (got %d)", scene.celBands());
    scene.setCelBands(200);
    CHECK(scene.celBands() == TD_CEL_MAX_BANDS, "bands 200 must clamp to the ceiling (got %d)",
          scene.celBands());
    scene.setCelInk(TD_CEL_INK_WHITE);
    CHECK(scene.celInk() == TD_CEL_INK_WHITE, "ink setter must round-trip");
  }

  // ---- Q4. quantizer signature ----
  // FNV-1a over band indices and the raw float bits of every level and
  // threshold, swept over bands x ambient x shade. pure arithmetic, no libm
  // — deterministic across platforms. a mismatch means the band math
  // changed; regenerate deliberately if so.
  {
    static const uint64_t QUANT_SIG = 0x962a660a38a3727fULL;
    uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    static const float ambients[3] = {0.0f, 0.15f, 0.5f};
    for (int n = 2; n <= TD_CEL_MAX_BANDS; ++n) {
      for (int ai = 0; ai < 3; ++ai) {
        float a = ambients[ai];
        uint8_t nb = (uint8_t)n;
        for (int k = 0; k <= 256; ++k) {
          h ^= (uint8_t)td_celBand((float)k / 256.0f, a, nb);
          h *= 1099511628211ULL;
        }
        for (int b = 0; b < n; ++b) {
          float lv = td_celLevel(b, a, nb);
          uint32_t bits;
          std::memcpy(&bits, &lv, sizeof(bits));
          for (int i = 0; i < 4; ++i) {
            h ^= (bits >> (8 * i)) & 0xFFu;
            h *= 1099511628211ULL;
          }
        }
        for (int k = 0; k <= n - 2; ++k) {
          float thr = td_celThresh(k, a, nb);
          uint32_t bits;
          std::memcpy(&bits, &thr, sizeof(bits));
          for (int i = 0; i < 4; ++i) {
            h ^= (bits >> (8 * i)) & 0xFFu;
            h *= 1099511628211ULL;
          }
        }
      }
    }
    CHECK(h == QUANT_SIG, "cel quantizer signature 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)h, (unsigned long long)QUANT_SIG);
  }

  // ---- Q5. golden cel frames through the full pipeline, every method ----
  // the K5 fixture rendered in TD_CEL at defaults (4 bands, black ink)
  // across all ten dithers — pins the span splitter, the band levels the
  // targets receive, and (first pins to do so) the outlineLoop/drawLineShade
  // ink path. regenerate only deliberately, never as a casual refresh.
  {
    static const uint64_t GOLDEN_CEL_FRAME[TD_DITHER_COUNT] = {
        0x2e9d4a689e4800c1ULL,  // BAYER8
        0x1f9846029b18502aULL,  // BAYER4
        0x665092a5d28fe6f2ULL,  // BAYER2
        0xae5a7d4ba9658957ULL,  // BLUE16
        0xaa7a8d544ed1ec50ULL,  // CLUST8
        0xac43c1f108df8e40ULL,  // IGN
        0x36f1db9e1360e2d8ULL,  // SCAN
        0x5abaa94e3564b61aULL,  // MONO
        0x666f0a69b6ce6d8fULL,  // FLOYD
        0xe3a7285a26d31443ULL,  // ATKINS
    };
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    for (int m = 0; m < TD_DITHER_COUNT; ++m) {
      const char *nm = td_ditherName((TD_Dither)m);
      CHECK(target.setDither((TD_Dither)m), "%s: setDither failed", nm);
      renderToChecked(scene, target, TD_CEL);
      CHECK(stats().count > 0, "%s: cel render is empty", nm);
      uint64_t hsh = fbHash();
      CHECK(hsh == GOLDEN_CEL_FRAME[m], "%s: cel frame hash 0x%llxULL != pinned 0x%llxULL", nm,
            (unsigned long long)hsh, (unsigned long long)GOLDEN_CEL_FRAME[m]);
    }
    target.setDither(TD_DITHER_BAYER8);
  }

  // ---- Q6. band + ink sweeps at BAYER8 ----
  // freezes the threshold/level math across band counts and proves the two
  // knobs reach pixels (every hash distinct from the 4-band default).
  {
    static const uint64_t GOLDEN_BANDS[3] = {
        0x4edc29e6a3eb6d16ULL,  // 2 bands
        0x24d46683565790e1ULL,  // 3 bands
        0x21ad78cd940ad27aULL,  // 8 bands
    };
    static const uint64_t GOLDEN_INK[2] = {
        0x44c77e9c043d6a0eULL,  // ink WHITE
        0x52213f41d0b5d664ULL,  // ink NONE
    };
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    renderToChecked(scene, target, TD_CEL);
    uint64_t h4 = fbHash();  // the 4-band black-ink default, pinned in Q5
    static const uint8_t bandN[3] = {2, 3, 8};
    for (int i = 0; i < 3; ++i) {
      scene.setCelBands(bandN[i]);
      renderToChecked(scene, target, TD_CEL);
      uint64_t hsh = fbHash();
      CHECK(hsh == GOLDEN_BANDS[i], "bands %d: hash 0x%llxULL != pinned 0x%llxULL", bandN[i],
            (unsigned long long)hsh, (unsigned long long)GOLDEN_BANDS[i]);
      CHECK(hsh != h4, "bands %d: band count must reach pixels", bandN[i]);
    }
    scene.setCelBands(4);
    static const TD_CelInk inks[2] = {TD_CEL_INK_WHITE, TD_CEL_INK_NONE};
    for (int i = 0; i < 2; ++i) {
      scene.setCelInk(inks[i]);
      renderToChecked(scene, target, TD_CEL);
      uint64_t hsh = fbHash();
      CHECK(hsh == GOLDEN_INK[i], "ink %d: hash 0x%llxULL != pinned 0x%llxULL", (int)inks[i],
            (unsigned long long)hsh, (unsigned long long)GOLDEN_INK[i]);
      CHECK(hsh != h4, "ink %d: ink style must reach pixels", (int)inks[i]);
    }
  }

  // ---- Q7. sentinel windows: materials bypass quantization ----
  // the M2 overlap fixture rendered in TD_CEL with ink NONE (black ink draws
  // 0.0 loop edges across every face, including WHITE ones, and an edge
  // through the window would break the exact counts): WHITE fills stay
  // 25/25 lit and a near BLACK object stays 0/25 under BAYER8 — the 0.0/2.0
  // sentinels ride the untouched span path, never the band quantizer.
  {
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      setupK5(scene, farObj);
      scene.setCelInk(TD_CEL_INK_NONE);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      renderToChecked(scene, target, TD_CEL);
      long boxFar = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) boxFar += fb[y][x];
      CHECK(boxFar == 25, "cel: WHITE far object should fill the window (%ld/25)", boxFar);
    }
    {
      TD_Scene scene;
      TD_Object farObj(barrel_mesh);
      TD_Object nearObj(barrel_mesh);
      setupK5(scene, farObj);
      scene.add(nearObj);
      scene.setCelInk(TD_CEL_INK_NONE);
      farObj.setPosition(0.0f, 0.0f, 0.9f);
      farObj.setMaterial(TD_MATERIAL_WHITE);
      nearObj.setRotation(TILT, 0.7f, 0.0f);
      nearObj.setPosition(0.0f, 0.0f, -0.9f);
      nearObj.setScale(0.5f);
      nearObj.setMaterial(TD_MATERIAL_BLACK);
      renderToChecked(scene, target, TD_CEL);
      long boxBoth = 0;
      for (int y = 14; y <= 18; ++y)
        for (int x = 62; x <= 66; ++x) boxBoth += fb[y][x];
      CHECK(boxBoth == 0, "cel: near BLACK object must win the overlap (%ld/25 lit)", boxBoth);
    }
    {
      // seam-level white pin: a WHITE object's cel spans must arrive as the
      // bitwise 2.0 sentinel, never the quantized 1.0 top-band level — every
      // dither saturates identically at both, so only the recorder can tell
      // the sentinel path from the quantizer.
      static CelSeamRecorder rec;
      TD_Scene scene;
      TD_Object obj(barrel_mesh);
      setupK5(scene, obj);
      scene.setCelInk(TD_CEL_INK_NONE);
      obj.setMaterial(TD_MATERIAL_WHITE);
      rec.reset(true);
      rec.allowed[rec.nAllowed++] = 2.0f;
      CHECK(renderTo(scene, rec, TD_CEL), "white sentinel render failed (status %d)",
            (int)scene.lastStatus());
      CHECK(rec.spanCalls > 0, "white sentinel scene drew no spans");
      CHECK(rec.badLevel == 0, "%d WHITE spans arrived off the bitwise 2.0 sentinel",
            rec.badLevel);
      CHECK(rec.badSlope == 0, "%d WHITE spans with a non-+0.0f slope", rec.badSlope);
    }
  }

  // ---- Q8. determinism: repeated cel renders match bit-exactly ----
  {
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    static uint8_t fbFirst[H][W];
    renderToChecked(scene, target, TD_CEL);
    CHECK(stats().count > 0, "cel determinism frame is empty");
    std::memcpy(fbFirst, fb, sizeof(fb));
    renderToChecked(scene, target, TD_CEL);
    CHECK(std::memcmp(fbFirst, fb, sizeof(fb)) == 0,
          "identical cel renders must match bit-exactly");
  }

  // ---- Q9. color target: N flat colors + the library's first color pin ----
  // pinned config: baseColor 0xFFFF, ambient 0.15, intensity 1.0, 4 bands,
  // the K5 pose (lit cap and backlit region both visible). the four band
  // levels modulate to exactly these RGB565 values; background and black-ink
  // pixels share 0x0000.
  {
    static const uint64_t GOLDEN_CEL_565 = 0x9a057d905e6629deULL;
    static uint16_t cfb[W * H];
    TD_Color565Target ct(cfb, (int16_t)W, (int16_t)H);
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    obj.setColor(0xFFFF);
    renderToChecked(scene, ct, TD_CEL);
    uint16_t seen[16];
    int nSeen = 0;
    for (int i = 0; i < W * H; ++i) {
      uint16_t c = cfb[i];
      if (c == 0x0000) continue;
      bool found = false;
      for (int s = 0; s < nSeen && !found; ++s) found = seen[s] == c;
      if (!found && nSeen < 16) seen[nSeen++] = c;
    }
    CHECK(nSeen == 4, "565 cel frame must show exactly celBands() flat colors (got %d)", nSeen);
    static const uint16_t expect[4] = {0x2124, 0x6B6D, 0xB5B6, 0xFFFF};
    for (int e = 0; e < 4; ++e) {
      bool found = false;
      for (int s = 0; s < nSeen && !found; ++s) found = seen[s] == expect[e];
      CHECK(found, "565 cel frame missing band color 0x%04X", expect[e]);
    }
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < W * H; ++i) {
      h ^= (uint8_t)(cfb[i] & 0xFF);
      h *= 1099511628211ULL;
      h ^= (uint8_t)(cfb[i] >> 8);
      h *= 1099511628211ULL;
    }
    CHECK(h == GOLDEN_CEL_565, "565 cel frame hash 0x%llxULL != pinned 0x%llxULL",
          (unsigned long long)h, (unsigned long long)GOLDEN_CEL_565);
  }

  // ---- Q10. screentone uniformity: a band renders as a periodic tile ----
  // matrix methods only: IGN's TD_DITHER_SPECS row is a never-read
  // placeholder and its noise has no integer period; the diffusion pair
  // reflows in endFrame. driving the span seam directly at each band level,
  // the frame must repeat with the method's rank-table period — the
  // bands-are-uniform-patterns property, independent of frame hashes.
  {
    static const TD_Dither mm[7] = {TD_DITHER_BAYER8,   TD_DITHER_BAYER4, TD_DITHER_BAYER2,
                                    TD_DITHER_BLUE16,   TD_DITHER_CLUSTER8,
                                    TD_DITHER_SCANLINE, TD_DITHER_MONO};
    for (int mi = 0; mi < 7; ++mi) {
      TD_Dither m = mm[mi];
      const char *nm = td_ditherName(m);
      CHECK(target.setDither(m), "%s: setDither failed", nm);
      const TD_DitherSpec &sp = TD_DITHER_SPECS[m];
      for (int b = 0; b < 4; ++b) {
        float lv = td_celLevel(b, 0.15f, 4);
        clearFb();
        target.beginFrame();
        for (int y = 0; y < H; ++y)
          target.plotShadeSpan((int16_t)y, 0, (int16_t)(W - 1), 0.0f, lv, 0.0f);
        target.endFrame();
        long bad = 0;
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x)
            if (fb[y][x] != fb[y & sp.yMask][x & sp.xMask]) ++bad;
        CHECK(bad == 0, "%s band %d: tile not periodic (%ld mismatches)", nm, b, bad);
      }
    }
    target.setDither(TD_DITHER_BAYER8);
  }

  // ---- Q11. seam audit: cel traffic is flat sub-spans at exact levels ----
  // a recording target on the K5 cel frame (defaults: 4 bands, black ink):
  // every span arrives with bitwise +0.0f slope and a left shade bitwise on
  // the band-level/sentinel set, every per-pixel plot is ink at 0.0/2.0,
  // and everything lands in bounds.
  {
    static CelSeamRecorder rec;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    rec.reset(true);
    for (int b = 0; b < 4; ++b) rec.allowed[rec.nAllowed++] = td_celLevel(b, 0.15f, 4);
    rec.allowed[rec.nAllowed++] = 0.0f;
    rec.allowed[rec.nAllowed++] = 2.0f;
    CHECK(renderTo(scene, rec, TD_CEL), "seam audit render failed (status %d)",
          (int)scene.lastStatus());
    CHECK(rec.spanCalls > 0, "seam audit saw no spans");
    CHECK(rec.plotCalls > 0, "black ink must reach the per-pixel path");
    CHECK(rec.badSlope == 0, "%d spans arrived with a non-+0.0f slope", rec.badSlope);
    CHECK(rec.badLevel == 0, "%d spans arrived off the band-level set", rec.badLevel);
    CHECK(rec.badPlot == 0, "%d per-pixel plots outside the 0.0/2.0 ink sentinels", rec.badPlot);
    CHECK(rec.oob == 0, "%d seam calls out of bounds", rec.oob);
  }

  // ---- Q3b. span-overflow: huge span, tiny shade delta ----
  // one screen-spanning triangle whose corner shades straddle band threshold
  // 0 by ~4e-4 while its projected width is ~2.5e35 px: the pixels-per-shade
  // slope in celFillSpan overflows to inf on every crossing row. freezes the
  // overflow outcome — every covered pixel exactly one in-bounds write at an
  // exact band level, no crash, no UB. (with a shade strictly off the
  // threshold, the monotone clamp alone would deliver the same frame; the
  // guard's own load-bearing case is Q3c.)
  {
    static const float gv[3][3] = {{-1e34f, -0.5f, 0}, {1e34f, -0.5f, 0}, {0, 0.7f, 0}};
    static const uint16_t gt[1][3] = {{0, 2, 1}};
    // corner order follows the tri (0, 2, 1): corner 0 lights to ~0.2914
    // (band 0), the apex and right corners to ~0.2919 (band 1); threshold 0
    // sits at ~0.2917 for ambient 0.15, 4 bands.
    static const float gn[3][3] = {{0, 0, -0.1664f}, {0, 0, -0.1669f}, {0, 0, -0.1669f}};
    static const uint16_t gf[1][4] = {{0, 1, 0, 3}};
    static const uint16_t gl[3] = {0, 2, 1};
    static const TD_Mesh giant = {gv, gt, gn, gf, gl, nullptr, 3, 1, 1, 3, 1.0f};
    static CelSeamRecorder rec;
    TD_Scene scene;
    TD_Object obj(giant);
    setupK5(scene, obj);
    obj.setRotation(0.0f, 0.0f, 0.0f);
    scene.light.setDirectionVector(0.0f, 0.0f, -1.0f);
    scene.setCelInk(TD_CEL_INK_NONE);
    rec.reset(true);
    for (int b = 0; b < 4; ++b) rec.allowed[rec.nAllowed++] = td_celLevel(b, 0.15f, 4);
    CHECK(renderTo(scene, rec, TD_CEL), "overflow-guard render failed (status %d)",
          (int)scene.lastStatus());
    long covered = 0, multi = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        if (rec.writes[y][x] > 0) ++covered;
        if (rec.writes[y][x] > 1) ++multi;
      }
    CHECK(covered > 1000, "giant triangle should cover full rows (%ld px)", covered);
    CHECK(multi == 0, "guarded rows must stay a partition: %ld pixels written twice", multi);
    CHECK(rec.badSlope == 0, "%d overflow spans with a non-+0.0f slope", rec.badSlope);
    CHECK(rec.badLevel == 0, "%d overflow spans off the band-level set", rec.badLevel);
    CHECK(rec.oob == 0, "%d overflow seam calls out of bounds", rec.oob);
  }

  // ---- Q3c. threshold-exact shade with infinite slope: the guard's
  // load-bearing case ----
  // a horizontal top edge whose LEFT corner shade is bitwise EQUAL to band
  // threshold 0 while the pixels-per-shade slope is inf: without the
  // isfinite guard, celFillSpan's crossing would compute
  // xc = xl + (thr - sl) * inf = xl + 0 * inf = NaN, both fillSpan calls
  // would bail on the NaN, and the whole row would silently drop — a G2
  // coverage crack. with the guard the row arrives as one flat sub-span.
  // construction: light (0,0,-1) at intensity 1, ambient 0.15, corner
  // normal z = -(0.5/3) makes the pinned lighting expression reproduce
  // td_celThresh(0, 0.15, 4) bitwise; focal 64, pz 4, corner y = 9/32
  // project the top edge to y exactly 11.5, so its row is row 11; verts at
  // x = +/-1e36 give span width ~3.2e37 over a ~2e-4 delta -> g = inf.
  {
    static const float hv[3][3] = {
        {-1e36f, 0.28125f, 0}, {1e36f, 0.28125f, 0}, {0, -0.4f, 0}};
    static const uint16_t ht[1][3] = {{0, 1, 2}};
    static const float hq = 0.5f / 3.0f;  // td_celThresh(0)'s midpoint ratio
    static const float hn[3][3] = {{0, 0, -hq}, {0, 0, -0.1669f}, {0, 0, -0.1669f}};
    static const uint16_t hf[1][4] = {{0, 1, 0, 3}};
    static const uint16_t hl[3] = {0, 1, 2};
    static const TD_Mesh topEdge = {hv, ht, hn, hf, hl, nullptr, 3, 1, 1, 3, 1.0f};
    // freeze the construction's premise: the renderer's lighting expression
    // shape lands bitwise on the threshold for this normal/light/ambient.
    float lit = 0.15f + (1.0f - 0.15f) * hq * 1.0f;
    CHECK(CelSeamRecorder::sameBits(lit, td_celThresh(0, 0.15f, 4)),
          "Q3c premise broke: lighting %.9g no longer reproduces threshold %.9g bitwise",
          lit, td_celThresh(0, 0.15f, 4));
    static CelSeamRecorder rec;
    TD_Scene scene;
    TD_Object obj(topEdge);
    scene.add(obj);
    scene.camera.lookAt({0.0f, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f});
    scene.camera.setFocalPixels(64.0f);
    scene.camera.setNearSkip(0.25f);
    scene.camera.setCenterPixels(W * 0.5f, H * 0.5f);
    scene.light.setDirectionVector(0.0f, 0.0f, -1.0f);
    scene.light.setIntensity(1.0f);
    scene.setAmbient(0.15f);
    scene.setCelInk(TD_CEL_INK_NONE);
    rec.reset(true);
    for (int b = 0; b < 4; ++b) rec.allowed[rec.nAllowed++] = td_celLevel(b, 0.15f, 4);
    CHECK(renderTo(scene, rec, TD_CEL), "threshold-exact render failed (status %d)",
          (int)scene.lastStatus());
    long topRow = 0;
    for (int x = 0; x < W; ++x)
      if (rec.writes[11][x] == 1) ++topRow;
    CHECK(topRow == W, "threshold-exact top row must arrive whole (%ld/%d written once)",
          topRow, W);
    CHECK(rec.badSlope == 0, "%d threshold-exact spans with a non-+0.0f slope", rec.badSlope);
    CHECK(rec.badLevel == 0, "%d threshold-exact spans off the band-level set", rec.badLevel);
    CHECK(rec.oob == 0, "%d threshold-exact seam calls out of bounds", rec.oob);
  }

  // ---- Q12. opt-out identity: cel knobs must not perturb other modes ----
  // the K5 BAYER8 golden hash restated locally (GOLDEN_FRAME is block-scoped
  // inside runRenderTests); a deliberate regeneration there must update this
  // constant too.
  {
    static const uint64_t K5_BAYER8 = 0xf6f7fda5c9698e6eULL;
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setCelBands(3);
    scene.setCelInk(TD_CEL_INK_WHITE);
    renderToChecked(scene, target, TD_SHADED);
    uint64_t hsh = fbHash();
    CHECK(hsh == K5_BAYER8, "cel knobs leaked into TD_SHADED: 0x%llxULL != K5 pin 0x%llxULL",
          (unsigned long long)hsh, (unsigned long long)K5_BAYER8);
  }

  // ---- Q13. partition identity: banding repartitions spans, never
  // re-rasterizes ----
  // with ink NONE, TD_CEL must write exactly the pixels TD_SHADED writes,
  // exactly as often — celFillSpan's sub-spans share boundary floats and
  // re-enter fillSpan's half-open clip, so any crack or double-draw shows as
  // a count mismatch. then the degradation identity: ambient 1.0 collapses
  // every band to the flat-lit frame.
  {
    static CelSeamRecorder rec;
    static uint16_t shadedWrites[H][W];
    TD_Scene scene;
    TD_Object obj(barrel_mesh);
    setupK5(scene, obj);
    scene.setCelInk(TD_CEL_INK_NONE);
    rec.reset(false);
    CHECK(renderTo(scene, rec, TD_SHADED), "partition baseline render failed");
    std::memcpy(shadedWrites, rec.writes, sizeof(shadedWrites));
    rec.reset(false);
    CHECK(renderTo(scene, rec, TD_CEL), "partition cel render failed");
    CHECK(std::memcmp(shadedWrites, rec.writes, sizeof(shadedWrites)) == 0,
          "cel sub-spans must cover exactly the shaded span pixels, exactly as often");
    scene.setAmbient(1.0f);
    static uint8_t fbShaded[H][W];
    renderToChecked(scene, target, TD_SHADED);
    std::memcpy(fbShaded, fb, sizeof(fb));
    renderToChecked(scene, target, TD_CEL);
    CHECK(std::memcmp(fbShaded, fb, sizeof(fb)) == 0,
          "TD_CEL at ambient 1.0 must degrade to the flat-lit TD_SHADED frame");
  }

  CHECK(oobCount == 0, "cel suite plotted out of bounds %ld times", oobCount);
  clearFb();
}
