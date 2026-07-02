// SPDX-License-Identifier: MIT
#include "TD_Color565Target.h"

// RGB565 framebuffer target; only the shade -> pixel tail lives here. three
// fill paths, selected by the material latched in setMaterial:
//   BLACK -> exact matBlack_ palette color, shade ignored
//   WHITE -> exact matWhite_ palette color, shade ignored
//   LIT   -> baseColor modulated per pixel by the interpolated shade
// the framebuffer is caller-owned; the caller pushes it to the panel after
// render().

TD_Color565Target::TD_Color565Target(uint16_t *framebuffer, int16_t w, int16_t h)
    : fb_(framebuffer), w_(w), h_(h) {}

void TD_Color565Target::setMatColors(uint16_t matBlack, uint16_t matWhite) {
  matBlack_ = matBlack;
  matWhite_ = matWhite;
}

void TD_Color565Target::clear(uint16_t color) {
  if (!fb_) return;
  const int32_t n = (int32_t)w_ * h_;
  for (int32_t i = 0; i < n; ++i) fb_[i] = color;
}

bool TD_Color565Target::beginFrame() {
  // a missing framebuffer must fail the frame here: the plot paths write
  // through fb_ unchecked (contract G1 covers coordinates, not storage),
  // and the renderer maps this false into TD_ERR_NO_TARGET.
  if (!fb_) return false;
  if (autoClear_) clear(background_);
  return true;
}

void TD_Color565Target::setMaterial(const TD_MaterialCtx &ctx) {
  // latch the face's material and base color for the spans/lines that
  // follow. this hook is the only way exact palette colors can exist:
  // shadeColor(base, 1.0) == base, which is not matWhite_ — an exact
  // material color is a palette decision unreachable from shade alone.
  mat_ = ctx.mat;
  base_ = ctx.baseColor;
}

void TD_Color565Target::plotShade(int16_t x, int16_t y, float shade) {
  // per-pixel path (lines, outlines). G1: coordinates arrive in-bounds, no
  // checks needed. T1/T2: a LIT shade <= 0 pixel is a write of 0x0000,
  // shade >= 1 writes base_ exactly.
  if (mat_ == TD_MATERIAL_BLACK) {
    fb_[(int32_t)y * w_ + x] = matBlack_;
  } else if (mat_ == TD_MATERIAL_WHITE) {
    fb_[(int32_t)y * w_ + x] = matWhite_;
  } else {
    fb_[(int32_t)y * w_ + x] = shadeColor(base_, shade);
  }
}

void TD_Color565Target::plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX,
                                      float leftShade, float shadeSlope) {
  // the fused span loops. G1: spans arrive clipped by construction; no
  // bounds checks here. BLACK/WHITE fill flat and never evaluate the shade;
  // LIT evaluates the seam-contract shade expression exactly as specified —
  // slope 0 delivers exactly leftShade, so flat-lit faces stay exact.
  uint16_t *row = fb_ + (int32_t)y * w_;
  if (mat_ == TD_MATERIAL_BLACK) {
    for (int x = xs; x <= xe; ++x) row[x] = matBlack_;
    return;
  }
  if (mat_ == TD_MATERIAL_WHITE) {
    for (int x = xs; x <= xe; ++x) row[x] = matWhite_;
    return;
  }
  for (int x = xs; x <= xe; ++x) {
    float s = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope;
    row[x] = shadeColor(base_, s);
  }
}

uint16_t TD_Color565Target::shadeColor(uint16_t base, float sh) {
  // fixed-point shade modulation, endpoint-exact by construction:
  // iq(0) = 0 -> 0x0000 exactly; iq(1) = 256 -> base unchanged.
  if (sh < 0.0f) sh = 0.0f;
  if (sh > 1.0f) sh = 1.0f;
  uint32_t iq = (uint32_t)(sh * 256.0f + 0.5f);  // 0..256 fixed point
  uint32_t r = (((base >> 11) & 0x1F) * iq) >> 8;
  uint32_t g = (((base >> 5) & 0x3F) * iq) >> 8;
  uint32_t b = ((base & 0x1F) * iq) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
