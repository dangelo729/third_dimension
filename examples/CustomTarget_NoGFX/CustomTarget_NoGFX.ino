// SPDX-License-Identifier: MIT
//
// CustomTarget_NoGFX — the zero-dependency path: no display library at all.
// a TD_MonoTarget built on a plain pixel callback renders the scene into a
// byte buffer, which is dumped as ASCII art over Serial once per second.
// this is the same interface the library's host test suite uses, and the
// starting point for driving any display the GFX ecosystem doesn't cover:
// the callback is the entire porting surface.
//
// requires: nothing beyond the ThirdDimension library itself.

#include <ThirdDimension.h>

#define W 64
#define H 32

// the render surface: one byte per pixel. the callback writes both states —
// dark pixels are writes, never skips (nearer faces must overwrite farther
// ones in the painter pass).
static uint8_t frame[H][W];

static void plotToBuffer(void *user, int16_t x, int16_t y, uint8_t on) {
  (void)user;
  frame[y][x] = on;  // in-bounds guaranteed by the renderer's contract
}

TD_MonoTarget target(plotToBuffer, nullptr, W, H);

TD_TorusMesh donutMesh;
TD_Object donut(donutMesh);
TD_Scene scene;

void setup() {
  Serial.begin(115200);
  scene.add(donut);
  target.setAutoClear(true);  // the default: beginFrame plots the dark state everywhere
}

void loop() {
  float t = millis() * 0.001f;              // seconds
  donut.setRotation(t * 0.7f, t * 1.1f, 0.0f);  // tumble (radians)

  scene.render(target);

  // ASCII dump: '#' lit, '.' dark.
  Serial.println(F("+----------------------------------------------------------------+"));
  for (int y = 0; y < H; ++y) {
    char row[W + 1];
    for (int x = 0; x < W; ++x) row[x] = frame[y][x] ? '#' : '.';
    row[W] = '\0';
    Serial.println(row);
  }

  delay(1000);
}
