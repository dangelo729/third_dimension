// SPDX-License-Identifier: MIT
//
// CelGallery — cel (toon) shading on a 1-bit display: a spinning torus with
// its lighting quantized to flat bands, each band a uniform screen-locked
// dither pattern (the manga-screentone look), plus switchable outline ink.
// three independent cycles run at different rates: the ordered dither
// methods (the stable-toon pairing — diffusion methods reflow band interiors
// under motion, so they are left out), the band count 2..6, and the ink
// style (black / white / none), all narrated over Serial.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C,
// display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

TD_TorusMesh torusMesh;  // fully smooth: band boundaries curve, never facet
TD_Object torus(torusMesh);
TD_Scene scene;

// the eight ordered methods, frame-stable under motion.
const TD_Dither ORDERED[8] = {TD_DITHER_BAYER8,   TD_DITHER_BAYER4, TD_DITHER_BAYER2,
                              TD_DITHER_BLUE16,   TD_DITHER_CLUSTER8, TD_DITHER_IGN,
                              TD_DITHER_SCANLINE, TD_DITHER_MONO};
const char *INK_NAMES[3] = {"ink: black", "ink: white", "ink: none"};

const uint32_t DITHER_MS = 3000;  // dwell per dither method
const uint32_t BANDS_MS = 15000;  // dwell per band count
const uint32_t INK_MS = 45000;    // dwell per ink style
uint8_t ditherIndex = 0, bandIndex = 0, inkIndex = 0;
uint32_t lastDitherMs = 0, lastBandsMs = 0, lastInkMs = 0;

void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  scene.add(torus);
  scene.setRenderMode(TD_CEL);
  scene.setAmbient(0.30f);  // lifts the darkest band clear of the black
                            // background (0.15 dithers too close to it)
  scene.light.setDirection(0.6f, 0.45f);  // oblique key light: several bands
                                          // visible at once
  scene.camera.setOrbit(0.0f, 0.35f);     // look-down shows the tube rings

  Serial.println(target.ditherName());
  Serial.println(F("bands: 4"));
  Serial.println(INK_NAMES[0]);
}

void loop() {
  uint32_t now = millis();
  if (now - lastDitherMs >= DITHER_MS) {
    ditherIndex = (ditherIndex + 1) % 8;
    target.setDither(ORDERED[ditherIndex]);  // ordered methods allocate nothing
    Serial.println(target.ditherName());
    lastDitherMs = now;
  }
  if (now - lastBandsMs >= BANDS_MS) {
    bandIndex = (bandIndex + 1) % 5;
    scene.setCelBands(2 + bandIndex);  // 2..6: past 6, coarse dithers merge bands
    Serial.print(F("bands: "));
    Serial.println(scene.celBands());
    lastBandsMs = now;
  }
  if (now - lastInkMs >= INK_MS) {
    inkIndex = (inkIndex + 1) % 3;
    scene.setCelInk((TD_CelInk)inkIndex);
    Serial.println(INK_NAMES[inkIndex]);
    lastInkMs = now;
  }

  torus.setRotation(0.4f, now * 0.0006f, 0.15f);  // tilt + slow spin (radians)

  scene.render(target);
  display.display();
}
