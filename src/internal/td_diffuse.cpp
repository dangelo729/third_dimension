// SPDX-License-Identifier: MIT
#include "td_diffuse.h"

#include "../TD_Dither.h"

// deposits error at one kernel tap; taps outside the frame are dropped
// (no wrap). the unsigned compare funnels negative and past-the-edge
// coordinates into a single branch.
static inline void tdSpread(float *err, int16_t w, int16_t h, int x, int y, float e) {
  if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) err[y * w + x] += e;
}

bool td_diffuseFrame(const float *shade, float *err, int16_t w, int16_t h, TD_Dither method,
                     TD_DiffusePlotFn plot, void *user) {
  if (!shade || !err || !plot || w <= 0 || h <= 0 || !td_ditherIsDiffusion(method)) return false;
  // err is caller scratch: cleared here so consecutive frames never inherit
  // stale error.
  for (int32_t i = 0; i < (int32_t)w * h; ++i) err[i] = 0.0f;

  for (int y = 0; y < h; ++y) {
    // serpentine: odd rows scan right-to-left with the kernel mirrored,
    // breaking up the directional worm artifacts of plain raster order
    const int dx = (y & 1) ? -1 : 1;
    int x = (y & 1) ? w - 1 : 0;
    for (int k = 0; k < w; ++k, x += dx) {
      const int32_t i = (int32_t)y * w + x;
      const float v = shade[i];
      if (v < 0.0f) continue;  // untouched by geometry: skip plot, absorb error
      if (v <= 0.0f) {  // hard off, absorbs error
        plot(user, (int16_t)x, (int16_t)y, 0);
        continue;
      }
      if (v >= 1.0f) {  // hard on, absorbs error
        plot(user, (int16_t)x, (int16_t)y, 1);
        continue;
      }
      const float val = v + err[i];
      const uint8_t on = val > 0.5f;  // strict >, matching MONO's threshold
      plot(user, (int16_t)x, (int16_t)y, on);
      float e = val - (float)on;
      if (method == TD_DITHER_ATKINSON) {
        e *= 0.125f;  // 6 taps of 1/8 — 1/4 of the error is dropped by design,
                      // giving the classic crushed-shadow / blown-highlight look
        tdSpread(err, w, h, x + dx, y, e);
        tdSpread(err, w, h, x + 2 * dx, y, e);
        tdSpread(err, w, h, x - dx, y + 1, e);
        tdSpread(err, w, h, x, y + 1, e);
        tdSpread(err, w, h, x + dx, y + 1, e);
        tdSpread(err, w, h, x, y + 2, e);
      } else {  // Floyd-Steinberg: the canonical 7/16, 3/16, 5/16, 1/16
        e *= 0.0625f;
        tdSpread(err, w, h, x + dx, y, 7.0f * e);
        tdSpread(err, w, h, x - dx, y + 1, 3.0f * e);
        tdSpread(err, w, h, x, y + 1, 5.0f * e);
        tdSpread(err, w, h, x + dx, y + 1, 1.0f * e);
      }
    }
  }
  return true;
}
