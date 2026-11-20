// SPDX-License-Identifier: MIT
// dither suite: invariants and golden pins for the pure decision surface
// (td_ditherOn / td_ditherName, section J) and for the error-diffusion frame
// pass on synthetic shade fields (td_diffuseFrame, section K). the pinned hash
// constants are signatures of these pure functions; a mismatch means the pixel
// pattern changed — regenerate deliberately or not at all.

#include <cmath>
#include <cstring>

#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "internal/td_diffuse.h"
#include "test_common.h"

void runDitherTests() {
  // the standard cycling idiom and the golden tables index TD_Dither by
  // value, and the mono target must come up on the default method
  {
    TD_MonoTarget t(tdTestPlot, nullptr, (int16_t)W, (int16_t)H);
    CHECK(t.dither() == TD_DITHER_BAYER8, "TD_MonoTarget default dither must be Bayer 8x8");
  }

  // ---- J1. Bayer-8 must be bit-identical to the direct formula ----
  // (shade * 64 > BAYER8[y&7][x&7] + 0.5), including at the thresholds
  {
    static const uint8_t M8[8][8] = {
        {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
        {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
        {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
        {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
    };
    long mismatches = 0;
    for (int y = 0; y < 16; ++y)      // past the 8x8 tile: pins the y&7/x&7
      for (int x = 0; x < 16; ++x) {  // periodicity (mask bugs), not just one tile
        for (int k = -8; k <= 1032; ++k) {  // dense sweep, past both endpoints
          float s = k / 1024.0f;
          uint8_t want = s * 64.0f > (float)M8[y & 7][x & 7] + 0.5f;
          if (td_ditherOn(x, y, s, TD_DITHER_BAYER8) != want) ++mismatches;
        }
        for (int b = 0; b < 64; ++b) {  // and exactly at/around every threshold
          float th = (b + 0.5f) / 64.0f;
          float probes[3] = {std::nextafter(th, 0.0f), th, std::nextafter(th, 1.0f)};
          for (float s : probes) {
            uint8_t want = s * 64.0f > (float)M8[y & 7][x & 7] + 0.5f;
            if (td_ditherOn(x, y, s, TD_DITHER_BAYER8) != want) ++mismatches;
          }
        }
      }
    CHECK(mismatches == 0, "Bayer-8 not bit-identical to the direct formula (%ld mismatches)",
          mismatches);
  }

  // the HUD shows td_ditherName(method): pin the name<->method mapping
  static const char *const EXPECT_NAMES[] = {"BAYER8", "BAYER4", "BAYER2", "BLUE16",
                                             "CLUST8", "IGN",    "SCAN",   "MONO",
                                             "FLOYD",  "ATKINS"};
  static_assert(sizeof(EXPECT_NAMES) / sizeof(*EXPECT_NAMES) == TD_DITHER_COUNT,
                "EXPECT_NAMES out of date with TD_Dither");

  for (int m = 0; m < TD_DITHER_COUNT; ++m) {
    TD_Dither dm = (TD_Dither)m;
    const char *nm = td_ditherName(dm);
    CHECK(nm && std::strcmp(nm, EXPECT_NAMES[m]) == 0,
          "method %d: name \"%s\", expected \"%s\"", m, nm ? nm : "(null)", EXPECT_NAMES[m]);

    // ---- J2. endpoints, every screen pixel: shade<=0 off, shade>=1 on ----
    long badEnd = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        if (td_ditherOn(x, y, -1.0f, dm) || td_ditherOn(x, y, 0.0f, dm)) ++badEnd;
        if (!td_ditherOn(x, y, 1.0f, dm) || !td_ditherOn(x, y, 2.0f, dm)) ++badEnd;
      }
    CHECK(badEnd == 0, "%s: %ld endpoint violations", nm, badEnd);

    // ---- J3. monotone in shade at every pixel (once on, stays on) ----
    long badMono = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < 64; ++x) {  // 64x32 covers every table period
        uint8_t prev = 0;
        for (int k = 0; k <= 1024; ++k) {
          uint8_t on = td_ditherOn(x, y, k / 1024.0f, dm);
          if (on < prev) ++badMono;
          prev = on;
        }
      }
    CHECK(badMono == 0, "%s: %ld monotonicity violations", nm, badMono);

    // ---- J4. density tracks shade across the full screen ----
    // tolerance = the method's quantization step; IGN is statistical. MONO
    // is the deliberate exception — an exact all-or-nothing step at shade
    // 0.5 — and the error-diffusion pair behaves identically here because
    // their td_ditherOn is the bare quantizer (real diffusion is pinned in K).
    static const double tol[TD_DITHER_COUNT] = {0.02, 0.04, 0.13, 0.02, 0.02,
                                                0.05, 0.13, 0.0,  0.0,  0.0};
    bool step = dm == TD_DITHER_MONO || dm == TD_DITHER_FLOYD || dm == TD_DITHER_ATKINSON;
    for (int k = 0; k <= 16; ++k) {
      float s = k / 16.0f;
      long lit = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) lit += td_ditherOn(x, y, s, dm);
      double density = lit / (double)(W * H);
      if (step)
        CHECK(density == (s > 0.5f ? 1.0 : 0.0),
              "%s: density %.3f at shade %.3f, expected a hard step at 0.5", nm, density, s);
      else
        CHECK(std::fabs(density - s) <= tol[m],
              "%s: density %.3f at shade %.3f (tol %.2f)", nm, density, s, tol[m]);
    }
  }

  // ---- J8. golden tile signatures ----
  // FNV-1a over each method's 16x16 tile at every shade k/256 pins the
  // complete threshold function — table values, masks, shifts, scale/bias
  // placement, and spatial arrangement. a hash mismatch means the method's
  // pixel pattern changed; regenerate deliberately if so.
  {
    static const uint64_t GOLDEN[TD_DITHER_COUNT] = {
        0xae83877432945b83ULL,  // BAYER8
        0xbbd1bb73962cbf83ULL,  // BAYER4
        0x47f824462bb84f83ULL,  // BAYER2
        0x9c4c5ef058a6479dULL,  // BLUE16
        0xa4b2f6dc86517a83ULL,  // CLUST8
        0x8d8374d60e99dbe3ULL,  // IGN
        0x2ae548fc56428f83ULL,  // SCAN
        0x50576d65f0328f83ULL,  // MONO
        0x50576d65f0328f83ULL,  // FLOYD: td_ditherOn is the bare quantizer = MONO's pattern
        0x50576d65f0328f83ULL,  // ATKINS: same (the diffusion itself is pinned in K)
    };
    for (int m = 0; m < TD_DITHER_COUNT; ++m) {
      uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
      for (int k = 0; k <= 256; ++k)
        for (int y = 0; y < 16; ++y)
          for (int x = 0; x < 16; ++x) {
            h ^= td_ditherOn(x, y, k / 256.0f, (TD_Dither)m);
            h *= 1099511628211ULL;
          }
      CHECK(h == GOLDEN[m], "%s: tile signature 0x%llxULL != pinned 0x%llxULL",
            td_ditherName((TD_Dither)m), (unsigned long long)h,
            (unsigned long long)GOLDEN[m]);
    }
  }

  // SCANLINE must be horizontal: every row uniformly on or off, any shade
  for (int k = 1; k < 16; ++k)
    for (int y = 0; y < 8; ++y)
      for (int x = 1; x < 16; ++x)
        CHECK(td_ditherOn(x, y, k / 16.0f, TD_DITHER_SCANLINE) ==
                  td_ditherOn(0, y, k / 16.0f, TD_DITHER_SCANLINE),
              "SCANLINE: row %d not uniform at shade %d/16 (x=%d)", y, k, x);

  // ---- J7. out-of-range methods fall back to Bayer-8 ----
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      CHECK(td_ditherOn(x, y, 0.4f, (TD_Dither)200) ==
                td_ditherOn(x, y, 0.4f, TD_DITHER_BAYER8),
            "out-of-range method must clamp to Bayer-8");

  // ---- K. error diffusion (FLOYD/ATKINS): the frame pass itself ----
  // td_ditherOn for these methods is just the 0.5 quantizer (covered by J);
  // everything diffusion-specific goes through td_diffuseFrame, so it is
  // pinned here on synthetic shade fields.
  {
    static float field[H * W];
    static float errBuf[H * W];  // caller-provided scratch; cleared by the pass
    static const TD_Dither ED[2] = {TD_DITHER_FLOYD, TD_DITHER_ATKINSON};

    // ordered methods and null arguments must be rejected (no silent
    // mis-dispatch, no null dereference)
    CHECK(!td_diffuseFrame(field, errBuf, W, H, TD_DITHER_BAYER8, tdTestPlot, nullptr),
          "td_diffuseFrame must reject non-diffusion methods");
    CHECK(!td_diffuseFrame(nullptr, errBuf, W, H, TD_DITHER_FLOYD, tdTestPlot, nullptr),
          "td_diffuseFrame must reject a null shade field");
    CHECK(!td_diffuseFrame(field, nullptr, W, H, TD_DITHER_FLOYD, tdTestPlot, nullptr),
          "td_diffuseFrame must reject a null error buffer");
    CHECK(!td_diffuseFrame(field, errBuf, W, H, TD_DITHER_FLOYD, nullptr, nullptr),
          "td_diffuseFrame must reject a null plot sink");

    for (int e = 0; e < 2; ++e) {
      TD_Dither dm = ED[e];
      const char *nm = td_ditherName(dm);

      // ---- K1. endpoints: uniform 0 lights nothing, uniform 1 everything ----
      for (int i = 0; i < H * W; ++i) field[i] = 0.0f;
      clearFb();
      CHECK(td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr),
            "%s: diffuse frame failed", nm);
      CHECK(stats().count == 0, "%s: shade 0 lit %ld px", nm, stats().count);
      for (int i = 0; i < H * W; ++i) field[i] = 1.0f;
      clearFb();
      td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
      CHECK(stats().count == (long)W * H, "%s: shade 1 lit only %ld px", nm, stats().count);

      // ---- K2. untouched (<0) pixels: never plotted, absorb error ----
      // left half mid-grey, right half untouched; 2 = "was plotted" tracer.
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) field[y * W + x] = x < W / 2 ? 0.5f : -1.0f;
      std::memset(fb, 2, sizeof(fb));
      td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
      long touchedRight = 0, litLeft = 0, skippedLeft = 0;
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W / 2; ++x) {
          litLeft += fb[y][x] == 1;
          skippedLeft += fb[y][x] == 2;  // every covered pixel must be
        }                                // plotted (on or off), never skipped
        for (int x = W / 2; x < W; ++x) touchedRight += fb[y][x] != 2;
      }
      CHECK(touchedRight == 0, "%s: %ld untouched pixels were plotted", nm, touchedRight);
      CHECK(skippedLeft == 0, "%s: %ld covered pixels were never plotted", nm, skippedLeft);
      CHECK(litLeft > 0, "%s: mid-grey region lit nothing", nm);

      // ---- K3. no bleed: a shade-0 region bordering grey stays dark ----
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) field[y * W + x] = x < W / 2 ? 0.6f : 0.0f;
      clearFb();
      td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
      long litRight = 0;
      for (int y = 0; y < H; ++y)
        for (int x = W / 2; x < W; ++x) litRight += fb[y][x];
      CHECK(litRight == 0, "%s: error bled into a shade-0 region (%ld px)", nm, litRight);

      // ---- K3b. absorb means error can't tunnel either ----
      // with a 4px shade-0 stripe between two grey regions (no kernel
      // reaches across 4 cells, and shade-0 pixels must absorb, not relay),
      // the pattern right of the stripe must be exactly independent of
      // what's left of it. a mutant that lets shade-0 pixels propagate error
      // relays it across the stripe and shifts the right side — invisible to
      // density checks, caught here. grey 0.6, not 0.5: at exactly 0.5 the
      // relayed error happens to cancel.
      static uint8_t fbStripe[H][W];
      const int s0 = W / 2 - 2, s1 = W / 2 + 2;  // [s0,s1) = the stripe
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
          field[y * W + x] = (x >= s0 && x < s1) ? 0.0f : 0.6f;
      clearFb();
      td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
      std::memcpy(fbStripe, fb, sizeof(fb));
      long litStripe = 0;
      for (int y = 0; y < H; ++y)
        for (int x = s0; x < s1; ++x) litStripe += fbStripe[y][x];
      CHECK(litStripe == 0, "%s: the shade-0 stripe lit up (%ld px)", nm, litStripe);
      for (int y = 0; y < H; ++y)  // same field, left side zeroed out
        for (int x = 0; x < s0; ++x) field[y * W + x] = 0.0f;
      clearFb();
      td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
      long relayDiff = 0;
      for (int y = 0; y < H; ++y)
        for (int x = s1; x < W; ++x) relayDiff += fb[y][x] != fbStripe[y][x];
      CHECK(relayDiff == 0,
            "%s: error tunneled through the shade-0 stripe (%ld px differ)", nm, relayDiff);

      // ---- K4. uniform fields: lit count monotone in shade ----
      // FS conserves brightness (density ~ shade — only edge falloff is
      // lost). Atkinson drops 1/4 of the error, so its uniform-field steady
      // state is val = 4*shade - 3*on: the all-off state holds for shades
      // <= 1/8 (val <= 0.5 from below, strict > never fires) and the all-on
      // state needs 4s-3 > 0.5, i.e. s > 7/8 — at s = 7/8 the all-on margin
      // only decays asymptotically toward the threshold, so on this 32-row
      // panel d == 1.0 exactly (a taller target would eventually flip a
      // pixel; the pin is panel-specific, like the golden hashes). the
      // classic-Mac crush, pinned as the method's signature.
      long prev = -1;
      for (int k = 0; k <= 16; ++k) {
        float s = k / 16.0f;
        for (int i = 0; i < H * W; ++i) field[i] = s;
        clearFb();
        td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
        long lit = stats().count;
        CHECK(lit >= prev, "%s: density not monotone at shade %.3f (%ld < %ld)",
              nm, s, lit, prev);
        prev = lit;
        double d = lit / (double)(W * H);
        if (dm == TD_DITHER_FLOYD && k > 0 && k < 16)
          CHECK(std::fabs(d - s) <= 0.02, "%s: density %.3f at shade %.3f", nm, d, s);
        if (dm == TD_DITHER_ATKINSON) {
          if (k <= 2)
            CHECK(d == 0.0, "%s: shade %.3f should crush to black, got %.3f", nm, s, d);
          else if (k >= 14)
            CHECK(d == 1.0, "%s: shade %.3f should blow to white, got %.3f", nm, s, d);
          else
            CHECK(d > 0.0 && d < 1.0, "%s: mid shade %.3f collapsed to %.3f", nm, s, d);
        }
      }
    }

    // ---- K6. independent from-spec reference implementation ----
    // pins the kernels, the serpentine mirroring, scan order and absorb
    // rules analytically, so a kernel bug can't slip through a casual
    // "hash changed, regenerate" on the frame goldens. structurally
    // different from the engine (tap tables, 2-D error matrix); the float
    // ops agree bit-for-bit because the engine's two-step weight scaling
    // (e *= 1/16 or 1/8, then * the numerator) is exact power-of-two
    // scaling, equal to multiplying by the exact w/16 or 1/8 constants here.
    {
      struct Tap { int dx, dy; float w; };
      static const Tap FS_TAPS[4] = {{1, 0, 7 / 16.0f},
                                     {-1, 1, 3 / 16.0f},
                                     {0, 1, 5 / 16.0f},
                                     {1, 1, 1 / 16.0f}};
      static const Tap ATK_TAPS[6] = {{1, 0, 1 / 8.0f},  {2, 0, 1 / 8.0f},
                                      {-1, 1, 1 / 8.0f}, {0, 1, 1 / 8.0f},
                                      {1, 1, 1 / 8.0f},  {0, 2, 1 / 8.0f}};
      static float refErr[H][W];
      static uint8_t refFb[H][W];
      uint32_t savedLcg = lcg;  // keep the render suite's fuzz stream unchanged
      for (int trial = 0; trial < 50; ++trial) {
        for (int i = 0; i < H * W; ++i) {  // sentinels, endpoints, saturation, greys
          float r = frand(0.0f, 1.0f);
          field[i] = r < 0.10f   ? -1.0f
                     : r < 0.20f ? 0.0f
                     : r < 0.30f ? 1.0f
                     : r < 0.35f ? 1.5f
                                 : frand(0.0f, 1.0f);
        }
        for (int e = 0; e < 2; ++e) {
          TD_Dither dm = ED[e];
          const Tap *taps = dm == TD_DITHER_ATKINSON ? ATK_TAPS : FS_TAPS;
          const int ntaps = dm == TD_DITHER_ATKINSON ? 6 : 4;
          std::memset(refErr, 0, sizeof(refErr));
          std::memset(refFb, 2, sizeof(refFb));  // 2 = "not plotted"
          for (int y = 0; y < H; ++y) {
            const int dir = (y & 1) ? -1 : 1;
            for (int k = 0; k < W; ++k) {
              const int x = (y & 1) ? W - 1 - k : k;
              const float v = field[y * W + x];
              if (v < 0.0f) continue;  // untouched: no plot, error dies here
              uint8_t on;
              float spill = 0.0f;
              if (v <= 0.0f) on = 0;  // forced endpoints absorb error
              else if (v >= 1.0f) on = 1;
              else {
                float val = v + refErr[y][x];
                on = val > 0.5f;
                spill = val - (float)on;
              }
              refFb[y][x] = on;
              if (spill != 0.0f)
                for (int t = 0; t < ntaps; ++t) {
                  int tx = x + taps[t].dx * dir, ty = y + taps[t].dy;
                  if (tx >= 0 && tx < W && ty >= 0 && ty < H)
                    refErr[ty][tx] += spill * taps[t].w;
                }
            }
          }
          std::memset(fb, 2, sizeof(fb));
          td_diffuseFrame(field, errBuf, W, H, dm, tdTestPlot, nullptr);
          long diff = 0;
          for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) diff += fb[y][x] != refFb[y][x];
          CHECK(diff == 0, "%s: trial %d disagrees with the reference (%ld px)",
                td_ditherName(dm), trial, diff);
        }
      }
      lcg = savedLcg;
    }
  }

  clearFb();
}
