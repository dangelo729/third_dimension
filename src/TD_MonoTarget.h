// SPDX-License-Identifier: MIT
#ifndef TD_MONOTARGET_H
#define TD_MONOTARGET_H

#include <stdint.h>

#include "TD_Dither.h"
#include "TD_RenderTarget.h"

// 1-bit render target: realizes shade values as dithered pixels through a
// plot callback (or a subclass's plotPixel override — see the GFX adapter).
// owns all dither state, including the error-diffusion frame buffers, because
// only a 1-bit surface dithers and only the target knows its dimensions.
//
// ordered methods dither inline in the span loop (rank row hoisted per span,
// decision bit-identical to td_ditherOn). FLOYD/ATKINSON record every span's
// shades into a frame-sized field instead — painter overwrites keep the
// nearest face's shade — and endFrame() quantizes the whole frame in one
// serpentine diffusion pass (internal/td_diffuse.h): untouched pixels are
// never written and absorb incoming error, so diffusion cannot bleed outside
// silhouettes and multiple renderPass() calls per frame still compose.
//
// materials reach mono purely through shade encoding (BLACK = 0.0, WHITE =
// 2.0 — the dither endpoint guarantees make BLACK speckle-free and WHITE
// drop-free), so setMaterial stays a no-op and baseColor is ignored by
// definition.
class TD_MonoTarget : public TD_RenderTarget {
 public:
  // on = 1 lit, on = 0 dark. dark pixels are writes, never skips:
  // nearer faces must overwrite farther ones in the painter pass.
  typedef void (*PlotFn)(void *user, int16_t x, int16_t y, uint8_t on);

  TD_MonoTarget(PlotFn plot, void *user, int16_t w, int16_t h);
  ~TD_MonoTarget();
  TD_MonoTarget(const TD_MonoTarget &) = delete;
  TD_MonoTarget &operator=(const TD_MonoTarget &) = delete;

  // selecting a diffusion method allocates the w*h shade + error float
  // buffers here (8 bytes/pixel — 64 KB at 128x64; see extras/docs/limits.md)
  // so the failure is visible at setup, never mid-frame. returns false and
  // keeps the previous method if allocation fails. buffers persist across
  // method switches once grown.
  bool setDither(TD_Dither m);
  TD_Dither dither() const { return method_; }
  const char *ditherName() const;

  // pre-allocate the diffusion buffers in setup() without switching method.
  bool reserve();

  // beginFrame plots the dark state across the frame by default so painter
  // output composes on a clean field; disable to composite 3-D over an
  // existing background/HUD (the renderer only writes covered pixels).
  void setAutoClear(bool on) { autoClear_ = on; }

  int16_t width() override { return w_; }
  int16_t height() override { return h_; }
  bool beginFrame() override;
  void plotShade(int16_t x, int16_t y, float shade) override;
  void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX, float leftShade,
                     float shadeSlope) override;
  void endFrame() override;

 protected:
  // subclass flavor (e.g. the GFX adapter): construct without a callback and
  // override plotPixel instead; update dimensions from the subclass's
  // beginFrame via setSize before calling the base beginFrame.
  TD_MonoTarget(int16_t w, int16_t h);
  virtual void plotPixel(int16_t x, int16_t y, uint8_t on);
  void setSize(int16_t w, int16_t h);

 private:
  PlotFn plot_ = nullptr;
  void *user_ = nullptr;
  int16_t w_ = 0, h_ = 0;
  TD_Dither method_ = TD_DITHER_BAYER8;
  bool autoClear_ = true;
  bool edActive_ = false;   // this frame routes span shades into shade_
  float *shade_ = nullptr;  // w*h; -1 = untouched-by-geometry sentinel
  float *err_ = nullptr;    // diffusion error accumulator
  int32_t capPix_ = 0;

  bool ensureFrame();  // grow shade_/err_ to w*h; false on allocation failure
};

#endif  // TD_MONOTARGET_H
