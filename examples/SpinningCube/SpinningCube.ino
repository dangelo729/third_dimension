// SPDX-License-Identifier: MIT
//
// SpinningCube — the minimal ThirdDimension sketch: one procedural cube,
// Gouraud-shaded and dithered onto an SSD1306 128x64 I2C OLED through the
// Adafruit_GFX adapter. everything else is a default: the camera auto-frames
// the scene at first render, the default key light shades it, and the target
// auto-clears each frame.
//
// requires: Adafruit GFX Library, Adafruit SSD1306 (install via Library
// Manager). wiring: standard I2C (SDA/SCL), display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);  // dithers 3-D shading onto any mono GFX display

TD_BoxMesh cubeMesh;      // unit cube, built in RAM at construction
TD_Object cube(cubeMesh);
TD_Scene scene;           // default light and auto-framing camera included

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  scene.add(cube);
}

void loop() {
  float t = millis() * 0.001f;             // seconds
  cube.setRotation(0.5f, t * 0.8f, 0.0f);  // fixed tilt + slow yaw spin (radians)

  scene.render(target);  // transform, sort, shade, dither, plot
  display.display();     // push the GFX buffer to the panel
}
