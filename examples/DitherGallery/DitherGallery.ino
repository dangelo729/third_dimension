// SPDX-License-Identifier: MIT
//
// DitherGallery — auto-cycles through all ten 1-bit dither methods on a
// spinning model, three seconds each, printing the active method's name over
// Serial. the ordered methods (Bayer, blue-noise, clustered-dot, IGN,
// scanline, mono) are frame-stable under motion; the two error-diffusion
// methods (FLOYD, ATKINSON) show the classic crawl. selecting a diffusion
// method allocates its frame-sized buffers on first use.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C,
// display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>
#include <models/TD_Model_Barrel.h>  // defines: const TD_Mesh barrel_mesh (flash arrays)

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

TD_Object barrel(barrel_mesh);
TD_Scene scene;

const uint32_t METHOD_MS = 3000;  // dwell per dither method
uint8_t ditherIndex = TD_DITHER_BAYER8;
uint32_t lastSwitchMs = 0;

void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  scene.add(barrel);
  scene.setAmbient(0.15f);
  scene.light.setDirection(0.6f, 0.45f);  // oblique key light: a wide spread
                                          // of mid shades shows the patterns
  scene.camera.setOrbit(0.0f, 0.25f);     // slight look-down (radians)

  Serial.println(target.ditherName());
}

void loop() {
  if (millis() - lastSwitchMs >= METHOD_MS) {
    ditherIndex = (ditherIndex + 1) % TD_DITHER_COUNT;
    if (target.setDither((TD_Dither)ditherIndex))
      Serial.println(target.ditherName());
    else
      Serial.println(F("dither buffer allocation failed; method unchanged"));
    lastSwitchMs = millis();
  }

  barrel.setRotation(0.12f, millis() * 0.0004f, 0.0f);  // tilt + slow spin (radians)

  scene.render(target);
  display.display();
}
