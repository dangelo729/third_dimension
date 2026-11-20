// SPDX-License-Identifier: MIT
//
// SpinningCube — the smallest complete sketch, and the README's: one cube,
// shaded and dithered onto a 128x64 SSD1306 OLED on I2C. the rest
// is defaults: the camera frames the scene at its first render, the key
// light shades it, and the target clears each frame.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: the OLED's SDA
// and SCL on the board's I2C pins, the panel at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);  // an I2C panel, no reset pin
TD_GFXMonoTarget target(display);  // dithers onto any 1-bit GFX display
TD_BoxMesh cubeMesh;               // a unit cube, built in RAM
TD_Object cube(cubeMesh);
TD_Scene scene;                    // camera and light included

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  scene.add(cube);
}

void loop() {
  cube.setRotation(-0.5f, millis() * 0.0008f, 0.0f);  // radians: tilt, spin
  scene.render(target);  // transform, sort, shade, dither, plot
  display.display();     // push the frame to the panel
}
