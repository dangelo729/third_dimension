// SPDX-License-Identifier: MIT
//
// ColorCube_SSD1331 — RGB565 path: the scene renders into a RAM framebuffer
// through TD_Color565Target (Gouraud shade modulates each object's base
// color), then the buffer is pushed to the panel in one SPI burst per frame.
// the SSD1331 has no read-back, so RAM compositing is mandatory.
//
// requires: Adafruit GFX Library, Adafruit SSD1331 OLED Driver Library for
// Arduino. wiring (hardware SPI): SCK/MOSI on the hardware SPI pins, plus the
// three control pins below.

#include <Adafruit_SSD1331.h>
#include <SPI.h>
#include <ThirdDimension.h>  // core only — this sketch renders to a raw buffer

#define OLED_CS 10   // chip select
#define OLED_DC 9    // data/command
#define OLED_RST 8   // reset

#define FB_W 96
#define FB_H 64

Adafruit_SSD1331 oled(&SPI, OLED_CS, OLED_DC, OLED_RST);

// engine-owned RGB565 framebuffer (96*64*2 = 12 KB).
static uint16_t framebuffer[FB_W * FB_H];
TD_Color565Target target(framebuffer, FB_W, FB_H);

TD_BoxMesh cubeMesh;
TD_Object cube(cubeMesh);
TD_Scene scene;

void setup() {
  oled.begin();

  target.setBackground(td_color565(6, 10, 30));  // deep-space blue-black
  cube.setColor(td_color565(236, 214, 150));     // warm cream, shaded by the light

  scene.add(cube);
  scene.setAmbient(0.12f);  // shade floor: the dark side keeps some color
}

void loop() {
  float t = millis() * 0.001f;             // seconds
  cube.setRotation(0.4f, t * 0.9f, 0.0f);  // tilt + spin (radians)

  scene.render(target);

  // push the framebuffer as one address window + a single pixel stream —
  // the fast path on every Adafruit SPI color driver.
  oled.startWrite();
  oled.setAddrWindow(0, 0, FB_W, FB_H);
  oled.writePixels(framebuffer, (uint32_t)FB_W * FB_H);
  oled.endWrite();
}
