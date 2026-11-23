// SPDX-License-Identifier: MIT
//
// opt-in Adafruit_GFX render targets. include this header (not
// ThirdDimension.h) in sketches that draw to a GFX display; it requires the
// "Adafruit GFX Library" to be installed.
//
// deliberately header-only and never included by the core umbrella: no .cpp
// under src/ references any GFX symbol, so the library core compiles
// identically whether or not GFX is installed (and with no Arduino headers
// at all on a host PC). the unconditional #include below is also what lets
// the Arduino builder and PlatformIO discover the GFX dependency for
// sketches that opt in.
#ifndef THIRDDIMENSION_GFX_H
#define THIRDDIMENSION_GFX_H

#include <Adafruit_GFX.h>

#include "ThirdDimension.h"

// dithered 3-D on any Adafruit_GFX display through drawPixel. designed for
// 1-bit panels (SSD1306 etc.) where lit/dark default to 1/0 — every mono
// driver's WHITE/BLACK; on a color panel, custom lit/dark colors give
// two-color dithering, at one drawPixel call per pixel drawn and, with
// auto-clear on, one per pixel of the panel to clear it: on an unbuffered
// panel each of those is a write over the bus, every frame.
//
// dimensions are read from the GFX object whenever the renderer asks for
// them, before it draws, so constructing the target before display.begin()
// is safe and a setRotation() lands in the next frame. a diffusion method
// sizes its buffers from the display when it is selected: select it after
// display.begin(). there is no begin() on this class by design — nothing to
// mis-order.
class TD_GFXMonoTarget : public TD_MonoTarget {
 public:
  explicit TD_GFXMonoTarget(Adafruit_GFX &gfx, uint16_t lit = 1, uint16_t dark = 0)
      : TD_MonoTarget(gfx.width(), gfx.height()), gfx_(gfx), lit_(lit), dark_(dark) {}

  // the display's size as it is now, the target's own size following it.
  int16_t width() override {
    setSize(gfx_.width(), gfx_.height());
    return TD_MonoTarget::width();
  }
  int16_t height() override {
    setSize(gfx_.width(), gfx_.height());
    return TD_MonoTarget::height();
  }

  // the diffusion buffers sized to the display as it is now.
  bool setDither(TD_Dither m) {
    setSize(gfx_.width(), gfx_.height());
    return TD_MonoTarget::setDither(m);
  }
  bool reserve() {
    setSize(gfx_.width(), gfx_.height());
    return TD_MonoTarget::reserve();
  }

  bool beginFrame() override {
    setSize(gfx_.width(), gfx_.height());
    return TD_MonoTarget::beginFrame();
  }

 protected:
  void plotPixel(int16_t x, int16_t y, uint8_t on) override {
    gfx_.drawPixel(x, y, on ? lit_ : dark_);
  }

 private:
  Adafruit_GFX &gfx_;
  uint16_t lit_, dark_;
};

// color 3-D on any Adafruit_GFX display through per-pixel drawPixel:
// universal but slow, and auto-clear via fillScreen flickers on unbuffered
// TFTs. for real speed composite into a GFXcanvas16 (or a raw buffer) with
// TD_Color565Target and push the buffer in one burst.
class TD_GFX565Target : public TD_RenderTarget {
 public:
  explicit TD_GFX565Target(Adafruit_GFX &gfx) : gfx_(gfx) {}

  void setBackground(uint16_t rgb565) { background_ = rgb565; }
  void setAutoClear(bool on) { autoClear_ = on; }
  void setMatColors(uint16_t matBlack, uint16_t matWhite) {
    matBlack_ = matBlack;
    matWhite_ = matWhite;
  }

  int16_t width() override { return gfx_.width(); }
  int16_t height() override { return gfx_.height(); }

  bool beginFrame() override {
    if (autoClear_) gfx_.fillScreen(background_);
    return true;
  }

  void setMaterial(const TD_MaterialCtx &ctx) override {
    mat_ = ctx.mat;
    base_ = ctx.baseColor;
  }

  void plotShade(int16_t x, int16_t y, float shade) override {
    if (mat_ == TD_MATERIAL_BLACK) {
      gfx_.drawPixel(x, y, matBlack_);  // exact palette color, shade ignored
      return;
    }
    if (mat_ == TD_MATERIAL_WHITE) {
      gfx_.drawPixel(x, y, matWhite_);
      return;
    }
    // LIT: per-channel fixed-point modulation of the face's base color,
    // exact at both endpoints (shade <= 0 -> 0x0000, shade >= 1 -> base).
    if (shade < 0.0f) shade = 0.0f;
    if (shade > 1.0f) shade = 1.0f;
    uint32_t iq = (uint32_t)(shade * 256.0f + 0.5f);
    uint32_t r = (((base_ >> 11) & 0x1F) * iq) >> 8;
    uint32_t g = (((base_ >> 5) & 0x3F) * iq) >> 8;
    uint32_t b = ((base_ & 0x1F) * iq) >> 8;
    gfx_.drawPixel(x, y, (uint16_t)((r << 11) | (g << 5) | b));
  }

 private:
  Adafruit_GFX &gfx_;
  uint16_t background_ = 0x0000;
  uint16_t matBlack_ = 0x0000, matWhite_ = 0xFFFF;
  uint16_t base_ = 0xFFFF;
  uint8_t mat_ = TD_MATERIAL_LIT;
  bool autoClear_ = true;
};

#endif  // THIRDDIMENSION_GFX_H
