// SPDX-License-Identifier: MIT
// GFX adapter suite: ThirdDimension_GFX.h's two targets on a stand-in
// Adafruit_GFX display (gfx_stub/Adafruit_GFX.h). the display's size is read
// when the renderer reads it, so a frame after setRotation() is laid out for
// the rotated panel, a display that learns its size in begin() draws from
// its first frame, and a diffusion method selected after begin() covers the
// whole panel: in each case the target draws exactly what a target made for
// the panel as it is now draws.

#include <cstring>

#include "ThirdDimension_GFX.h"

#include "test_common.h"

namespace {

// a display holding up to 128x128 pixels, counting every drawPixel outside
// its current size. begin() sizes it, as a driver that learns its panel
// there does.
struct GfxFake : Adafruit_GFX {
  static const int kMax = 128;
  uint16_t px[kMax * kMax];
  long oob = 0;

  GfxFake(int16_t w, int16_t h) : Adafruit_GFX(w, h) { std::memset(px, 0, sizeof px); }

  void begin(int16_t w, int16_t h) {
    WIDTH = _width = w;
    HEIGHT = _height = h;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= _width || y >= _height) {
      ++oob;
      return;
    }
    px[y * kMax + x] = color;
  }

  // forgets every pixel drawn, so a frame is compared alone.
  void wipe() {
    std::memset(px, 0, sizeof px);
    oob = 0;
  }

  long lit() const {
    long n = 0;
    for (int i = 0; i < kMax * kMax; ++i) n += px[i] != 0;
    return n;
  }
  int maxLitY() const {
    int m = -1;
    for (int i = 0; i < kMax * kMax; ++i)
      if (px[i] != 0 && i / kMax > m) m = i / kMax;
    return m;
  }
};

// pixels where two displays differ.
long gfxDiffer(const GfxFake &a, const GfxFake &b) {
  long n = 0;
  for (int i = 0; i < GfxFake::kMax * GfxFake::kMax; ++i) n += a.px[i] != b.px[i];
  return n;
}

}  // namespace

void runGfxTests() {
  TD_BoxMesh boxMesh;
  TD_Object box(boxMesh);
  TD_Scene scene;
  scene.add(box);
  box.setRotation(-0.5f, 0.6f, 0.0f);

  // ---- G1. a rotation lands in the next frame, every dither ----
  // a 128x64 panel turned to 64x128 between frames: the next frame plots
  // nothing outside the panel and is the frame a target made for the
  // turned panel draws.
  {
    static const TD_Dither methods[2] = {TD_DITHER_BAYER8, TD_DITHER_FLOYD};
    for (TD_Dither m : methods) {
      GfxFake panel(128, 64), fresh(128, 64);
      TD_GFXMonoTarget target(panel);
      CHECK(target.setDither(m), "setDither(%d) failed", (int)m);
      CHECK(scene.render(target), "the first frame failed");
      panel.setRotation(1);
      panel.wipe();
      CHECK(scene.render(target), "the frame after the rotation failed");
      fresh.setRotation(1);
      TD_GFXMonoTarget freshTarget(fresh);
      CHECK(freshTarget.setDither(m), "setDither(%d) failed", (int)m);
      CHECK(scene.render(freshTarget), "the fresh target's frame failed");
      CHECK(panel.oob == 0, "dither %d: %ld pixels plotted outside the turned panel", (int)m,
            panel.oob);
      CHECK(panel.maxLitY() >= 64,
            "dither %d: the frame is not laid out for 64x128 (lit to row %d)", (int)m,
            panel.maxLitY());
      CHECK(fresh.lit() > 100 && gfxDiffer(panel, fresh) == 0,
            "dither %d: the turned frame differs from a fresh target's in %ld px", (int)m,
            gfxDiffer(panel, fresh));
    }
  }

  // ---- G2. a display that is 0x0 until begin() ----
  // a target constructed before begin() renders from the first frame after
  // it, and a diffusion method selected after begin() diffuses the whole
  // panel, as a target constructed after begin() does.
  {
    static const TD_Dither methods[2] = {TD_DITHER_BAYER8, TD_DITHER_FLOYD};
    for (TD_Dither m : methods) {
      GfxFake panel(0, 0), fresh(0, 0);
      TD_GFXMonoTarget target(panel);
      panel.begin(128, 64);
      CHECK(target.setDither(m), "setDither(%d) failed", (int)m);
      const bool ok = scene.render(target);
      CHECK(ok, "dither %d: the first frame after begin() failed (status %d)", (int)m,
            (int)scene.lastStatus());
      fresh.begin(128, 64);
      TD_GFXMonoTarget freshTarget(fresh);
      CHECK(freshTarget.setDither(m), "setDither(%d) failed", (int)m);
      CHECK(scene.render(freshTarget), "the fresh target's frame failed");
      CHECK(fresh.lit() > 100 && gfxDiffer(panel, fresh) == 0,
            "dither %d: the frame differs from a fresh target's in %ld px", (int)m,
            gfxDiffer(panel, fresh));
      CHECK(panel.oob == 0, "dither %d: %ld pixels plotted outside the panel", (int)m, panel.oob);
    }
  }

  // ---- G3. a panel that outgrows the diffusion buffers mid-frame ----
  // in a caller's bracket, a pass after the panel grew past what FLOYD's
  // buffers cover takes the ordered path for the rest of the frame: it
  // draws, and nothing records past the buffers' end.
  {
    GfxFake panel(32, 16);
    TD_GFXMonoTarget target(panel);
    CHECK(target.setDither(TD_DITHER_FLOYD), "setDither(FLOYD) failed");
    CHECK(target.beginFrame(), "beginFrame failed");
    panel.begin(128, 64);
    CHECK(scene.renderer().renderPass(scene, scene.camera, target), "the pass failed");
    target.endFrame();
    CHECK(panel.lit() > 100, "the pass after the panel grew drew almost nothing (%ld px)",
          panel.lit());
    CHECK(panel.oob == 0, "%ld pixels plotted outside the grown panel", panel.oob);
  }

  // ---- G4. TD_GFX565Target reads the display's size as it is ----
  // after a rotation, and on a display sized in begin(), it draws exactly
  // what a target made for the panel as it is now draws.
  {
    GfxFake panel(128, 64), fresh(64, 128);
    TD_GFX565Target target(panel), freshTarget(fresh);
    CHECK(scene.render(target), "the first color frame failed");
    panel.setRotation(1);
    panel.wipe();
    CHECK(scene.render(target), "the color frame after the rotation failed");
    CHECK(scene.render(freshTarget), "the fresh color frame failed");
    CHECK(panel.oob == 0 && fresh.lit() > 100 && gfxDiffer(panel, fresh) == 0,
          "the turned color frame: %ld px outside, %ld px differ from a fresh target's",
          panel.oob, gfxDiffer(panel, fresh));

    GfxFake late(0, 0);
    TD_GFX565Target lateTarget(late);
    late.begin(64, 128);
    CHECK(scene.render(lateTarget), "the color frame after begin() failed (status %d)",
          (int)scene.lastStatus());
    CHECK(gfxDiffer(late, fresh) == 0, "the color frame after begin() differs in %ld px",
          gfxDiffer(late, fresh));
  }
}
