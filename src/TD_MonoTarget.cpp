// SPDX-License-Identifier: MIT
#include "TD_MonoTarget.h"

#include <stdlib.h>

#include "internal/td_diffuse.h"

// 1-bit target. ordered dither methods decide pixels inline (the decision
// stays bit-identical to td_ditherOn); the two diffusion methods record
// shades into a frame-sized field and quantize once in endFrame via
// td_diffuseFrame. all dither state — method, shade field, error buffer —
// lives here, never in the renderer.

TD_MonoTarget::TD_MonoTarget(PlotFn plot, void *user, int16_t w, int16_t h)
    : plot_(plot), user_(user), w_(w), h_(h) {}

TD_MonoTarget::TD_MonoTarget(int16_t w, int16_t h) : w_(w), h_(h) {
  // subclass flavor: no callback; pixels reach the subclass through its
  // plotPixel override.
}

TD_MonoTarget::~TD_MonoTarget() {
  // the diffusion buffers are target-owned (free(nullptr) is a no-op).
  free(shade_);
  free(err_);
}

bool TD_MonoTarget::setDither(TD_Dither m) {
  // diffusion methods need the w*h shade + error buffers; growing them here
  // keeps allocation failure visible at setup, never mid-frame. on failure
  // the previous method is kept. ordered methods never allocate, and the
  // buffers persist across method switches once grown. out-of-range values
  // are stored as-is: every consumer clamps to the Bayer-8 fallback.
  if (td_ditherIsDiffusion(m) && !ensureFrame()) return false;
  method_ = m;
  return true;
}

const char *TD_MonoTarget::ditherName() const { return td_ditherName(method_); }

bool TD_MonoTarget::reserve() {
  // setup-time pre-allocation of the diffusion buffers without switching
  // method; delegates to the same grower setDither uses.
  return ensureFrame();
}

bool TD_MonoTarget::beginFrame() {
  // diffusion recording arms only when the buffers really cover the current
  // dimensions (a subclass may have changed them via setSize after the last
  // setDither); otherwise the frame falls back through the ordered path,
  // where the diffusion methods behave as their bare 0.5 quantizer.
  edActive_ = td_ditherIsDiffusion(method_) && shade_ && err_ && capPix_ >= (int32_t)w_ * h_;
  if (autoClear_) {
    // dark pixels are writes, never skips: plotting the dark state across
    // the whole frame gives painter output a clean field. disabled when
    // compositing over an existing background/HUD.
    for (int16_t y = 0; y < h_; ++y)
      for (int16_t x = 0; x < w_; ++x) plotPixel(x, y, 0);
  }
  if (edActive_) {
    // -1 marks pixels untouched by geometry: never emitted, and absorbing
    // incoming diffusion error there keeps it from bleeding outside
    // silhouettes.
    const int32_t n = (int32_t)w_ * h_;
    for (int32_t i = 0; i < n; ++i) shade_[i] = -1.0f;
  }
  return true;
}

void TD_MonoTarget::plotShade(int16_t x, int16_t y, float shade) {
  // coordinates arrive in-bounds by renderer contract; no bounds checks.
  if (edActive_) {
    // record, not plot. clamp-at-store so a covered pixel can never
    // alias the -1 untouched sentinel: a plotted dark pixel must stay a
    // write. painter order = last write wins.
    shade_[(int32_t)y * w_ + x] = shade < 0.0f ? 0.0f : shade;
  } else {
    plotPixel(x, y, td_ditherOn(x, y, shade, method_));
  }
}

void TD_MonoTarget::plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX,
                                  float leftShade, float shadeSlope) {
  // the fused hot path. the shade at column x is
  //   leftShade + (x + 0.5f - leftEdgeX) * shadeSlope
  // computed with exactly this expression (the seam contract the bit-exact
  // golden tests pin); shadeSlope == 0 delivers exactly leftShade to every
  // pixel. spans arrive clipped, so neither loop bounds-checks.
  if (edActive_) {
    float *row = shade_ + (int32_t)y * w_;
    for (int x = xs; x <= xe; ++x) {
      float s = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope;
      row[x] = s < 0.0f ? 0.0f : s;  // same clamp-at-store as plotShade
    }
    return;
  }
  TD_Dither em = method_ < TD_DITHER_COUNT ? method_ : TD_DITHER_BAYER8;
  if (em == TD_DITHER_IGN) {
    // IGN is procedural — its TD_DITHER_SPECS row is a placeholder that
    // must never be read — so it takes the pure per-pixel decision.
    for (int x = xs; x <= xe; ++x) {
      float s = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope;
      plotPixel((int16_t)x, y, td_ditherOn(x, y, s, method_));
    }
    return;
  }
  // matrix methods: hoist the rank row once per span. adding the masked x
  // onto the shifted row base matches the flat table's OR indexing because
  // the low bits never overlap the shifted row bits, so the decision stays
  // bit-identical to td_ditherOn per pixel.
  const TD_DitherSpec &d = TD_DITHER_SPECS[em];
  const uint8_t *row = d.rank + ((y & d.yMask) << d.yShift);
  for (int x = xs; x <= xe; ++x) {
    float s = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope;
    int t = row[x & d.xMask] * d.scale + d.bias;
    plotPixel((int16_t)x, y, s * 256.0f > (float)t);
  }
}

namespace {
// diffusion output sink handed to td_diffuseFrame: casts user back to the
// target and forwards through the virtual plotPixel path so subclass
// overrides keep receiving pixels. plotPixel is protected, so the sink is a
// static member of a derived class — naming the member through the derived
// class grants access, and the member-pointer call still dispatches
// virtually.
struct TD_MonoDiffuseSink : TD_MonoTarget {
  static void plot(void *user, int16_t x, int16_t y, uint8_t on) {
    TD_MonoTarget *t = static_cast<TD_MonoTarget *>(user);
    (t->*(&TD_MonoDiffuseSink::plotPixel))(x, y, on);
  }
};
}  // namespace

void TD_MonoTarget::endFrame() {
  // diffusion quantization runs exactly once per frame bracket; ordered
  // methods plotted immediately and have nothing to finish.
  if (!edActive_) return;
  td_diffuseFrame(shade_, err_, w_, h_, method_, TD_MonoDiffuseSink::plot, this);
  edActive_ = false;
}

void TD_MonoTarget::plotPixel(int16_t x, int16_t y, uint8_t on) {
  // default pixel sink: the constructor-supplied callback. on = 0 is a
  // write of the dark state, never a skip. subclasses built with the
  // protected constructor override this; plot_ stays null there.
  if (plot_) plot_(user_, x, y, on);
}

void TD_MonoTarget::setSize(int16_t w, int16_t h) {
  // subclass hook: refresh dimensions (e.g. after a display rotation)
  // before calling the base beginFrame. ensureFrame sizes the diffusion
  // buffers against these dimensions.
  w_ = w;
  h_ = h;
}

bool TD_MonoTarget::ensureFrame() {
  // exact-fit high-water growth of the diffusion buffers. each pointer is
  // reassigned only when its own realloc succeeds — a failed grow leaves
  // the prior block valid — and the capacity is bumped only once both
  // buffers cover the frame, so a failed grow returns false and can be
  // retried. called from setDither()/reserve() only, never mid-frame.
  const int32_t need = (int32_t)w_ * h_;
  if (need <= capPix_) return true;
  float *ns = (float *)realloc(shade_, (size_t)need * sizeof(float));
  if (!ns) return false;
  shade_ = ns;
  float *ne = (float *)realloc(err_, (size_t)need * sizeof(float));
  if (!ne) return false;
  err_ = ne;
  capPix_ = need;
  return true;
}
