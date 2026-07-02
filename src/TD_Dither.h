// SPDX-License-Identifier: MIT
#ifndef TD_DITHER_H
#define TD_DITHER_H

#include <stdint.h>

#include "TD_Types.h"

// the dithering core: a standalone, instance-free component. TD_MonoTarget
// composes it; the host test harness pins it directly (golden tile
// signatures depend on these functions being reachable without a target).
//
// matrix methods share one code path: a rank table (each cell's lighting
// order, row-major, power-of-two period) plus per-method scale/bias mapping
// ranks onto a common 0..255 threshold range:
//
//     lit  iff  shade * 256 > rank * scale + bias
//
// the half-step bias makes the endpoints exact, and every ordered method is
// monotone in shade. Bayer-8 is bit-identical to the classic
// shade*64 > rank + 0.5 formula. IGN is the one procedural branch. the two
// error-diffusion methods expose MONO's bare 0.5 quantizer here — their real
// behavior is the frame post-pass (internal/td_diffuse.h) driven by
// TD_MonoTarget::endFrame.

// per-method threshold spec. exposed so the mono target's fused span loop
// can hoist the rank row once per span; treat as read-only.
struct TD_DitherSpec {
  const uint8_t *rank;   // row-major rank table, period (xMask+1) x (yMask+1)
  uint8_t xMask, yMask;  // period - 1 (periods are powers of two)
  uint8_t yShift;        // log2(xMask + 1): row stride in the flat table
  uint8_t scale, bias;   // threshold = rank * scale + bias
};

// indexed by TD_Dither; the FLOYD/ATKINSON entries hold MONO's spec.
extern const TD_DitherSpec TD_DITHER_SPECS[TD_DITHER_COUNT];

// the pure dither decision: 1 = lit. pure in (x, y, shade, method); shade<=0
// never lights and shade>=1 always lights for every method; out-of-range
// methods fall back to Bayer-8. for FLOYD/ATKINSON this is the bare 0.5
// quantizer (diffusion lives in the frame pass).
uint8_t td_ditherOn(int x, int y, float shade, TD_Dither method);

// short display name ("BAYER8", "FLOYD", ...) for HUDs and test dumps.
const char *td_ditherName(TD_Dither method);

static inline bool td_ditherIsDiffusion(TD_Dither m) {
  return m == TD_DITHER_FLOYD || m == TD_DITHER_ATKINSON;
}

#endif  // TD_DITHER_H
