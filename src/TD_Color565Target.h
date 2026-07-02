// SPDX-License-Identifier: MIT
#ifndef TD_COLOR565TARGET_H
#define TD_COLOR565TARGET_H

#include <stdint.h>

#include "TD_RenderTarget.h"

// RGB565 render target over a caller-owned framebuffer (row-major, w*h).
// shade modulates the current face's base color; the caller pushes the
// buffer to the panel after render() (many color panels have no read-back,
// so the frame must be composited in RAM and sent in one burst — e.g. the
// SSD1331 startWrite/setAddrWindow/writePixels/endWrite sequence).
//
// the target that needs setMaterial: LIT faces modulate the owning
// object's baseColor by the interpolated shade (fixed-point per-channel
// multiply, exact at both endpoints), while BLACK/WHITE faces fill the
// target's exact matBlack/matWhite palette colors, ignoring shade.
class TD_Color565Target : public TD_RenderTarget {
 public:
  TD_Color565Target(uint16_t *framebuffer, int16_t w, int16_t h);

  // beginFrame fills the framebuffer with this color (default 0x0000);
  // disable auto-clear to composite over an existing background.
  void setBackground(uint16_t rgb565) { background_ = rgb565; }
  void setAutoClear(bool on) { autoClear_ = on; }

  // exact colors for TD_MATERIAL_BLACK / TD_MATERIAL_WHITE faces.
  void setMatColors(uint16_t matBlack, uint16_t matWhite);

  // fill the whole framebuffer immediately (the auto-clear primitive,
  // exposed for manual frame composition).
  void clear(uint16_t color);

  int16_t width() override { return w_; }
  int16_t height() override { return h_; }
  bool beginFrame() override;
  void setMaterial(const TD_MaterialCtx &ctx) override;
  void plotShade(int16_t x, int16_t y, float shade) override;
  void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX, float leftShade,
                     float shadeSlope) override;

 private:
  uint16_t *fb_;
  int16_t w_, h_;
  uint16_t background_ = 0x0000;
  uint16_t matBlack_ = 0x0000, matWhite_ = 0xFFFF;
  uint16_t base_ = 0xFFFF;  // latched by setMaterial: current face base color
  uint8_t mat_ = TD_MATERIAL_LIT;
  bool autoClear_ = true;

  // darken an RGB565 color by a linear shade in [0,1] per channel, as
  // fixed-point iq = shade*256+0.5: iq(0) = 0 -> 0x0000 and iq(1) = 256 ->
  // base are exact by construction (the color-side endpoint guarantee).
  static uint16_t shadeColor(uint16_t base, float shade);
};

#endif  // TD_COLOR565TARGET_H
