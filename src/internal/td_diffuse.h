// SPDX-License-Identifier: MIT
#ifndef TD_INTERNAL_DIFFUSE_H
#define TD_INTERNAL_DIFFUSE_H

#include <stdint.h>

#include "../TD_Types.h"

// error-diffusion frame pass (TD_DITHER_FLOYD / TD_DITHER_ATKINSON).
// diffusion cannot run inside the per-face painter fills — it needs
// full-frame raster order and neighbor state — so the mono target records
// every span's shade into a frame-sized field (painter overwrites keep the
// nearest face's shade) and this pass quantizes the whole field afterwards.
//
// deliberately a free function, not a target method: the host harness pins
// the diffusion behavior on synthetic shade fields (kernels, serpentine
// mirroring, absorb rules) without constructing any target.
//
// field semantics (w*h floats, row-major):
//   entry <  0   untouched by geometry: never plotted, absorbs incoming
//                error — diffusion cannot bleed outside a silhouette, and
//                multiple passes per frame compose
//   entry <= 0   plotted dark unconditionally, absorbs error
//   entry >= 1   plotted lit unconditionally, absorbs error
//   otherwise    quantized at strict > 0.5 after adding accumulated error
//
// serpentine scan (odd rows right-to-left, kernel mirrored). kernels:
// Floyd-Steinberg 7/16, 3/16, 5/16, 1/16; Atkinson six taps of 1/8 (a
// quarter of the error is dropped by design — the crushed classic-Mac
// look). the output is a deterministic function of the field.

// on = 1 lit, on = 0 dark; dark pixels are writes (painter contract T1).
typedef void (*TD_DiffusePlotFn)(void *user, int16_t x, int16_t y, uint8_t on);

// `err` is a caller-provided w*h float scratch buffer (the mono target owns
// it alongside the shade field); it is cleared here before use. returns
// false for non-diffusion methods or null arguments.
bool td_diffuseFrame(const float *shade, float *err, int16_t w, int16_t h, TD_Dither method,
                     TD_DiffusePlotFn plot, void *user);

#endif  // TD_INTERNAL_DIFFUSE_H
