// SPDX-License-Identifier: MIT
//
// ImportedModel — display a model converted from OBJ (PicoCAD, Blender, or
// any exporter) with tools/obj2mesh.py; a button cycles through all ten
// dither methods, printing each method's name over Serial.
//
// regenerate the model header with:
//   python3 tools/obj2mesh.py extras/assets/tall_guy.obj \
//       -o src/models/TD_Model_TallGuy.h --name tall_guy
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C
// display at 0x3C; momentary button from pin 2 to GND (internal pull-up).

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>
#include <models/TD_Model_TallGuy.h>  // defines: const TD_Mesh tall_guy_mesh (flash arrays)

const uint8_t BUTTON_PIN = 2;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

TD_Object model(tall_guy_mesh);
TD_Scene scene;

uint8_t ditherIndex = TD_DITHER_BAYER8;
bool wasPressed = false;
uint32_t lastEdgeMs = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  scene.add(model);
  scene.setAmbient(0.18f);               // faint fill so the dark side isn't pure black
  scene.light.setDirection(0.8f, 0.45f); // key light up and to the left (az, el, radians)
  scene.camera.setOrbit(0.0f, 0.12f);    // tip the top of the model toward the camera
}

void loop() {
  // advance to the next dither method on each button press (50 ms debounce).
  // FLOYD/ATKINSON allocate their frame buffers inside setDither — the bool
  // reports allocation failure on RAM-tight boards (method kept unchanged).
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  if (pressed && !wasPressed && millis() - lastEdgeMs > 50) {
    ditherIndex = (ditherIndex + 1) % TD_DITHER_COUNT;
    if (target.setDither((TD_Dither)ditherIndex))
      Serial.println(target.ditherName());  // "BAYER8", "BLUE16", "FLOYD", ...
    else
      Serial.println(F("dither buffer allocation failed; method unchanged"));
    lastEdgeMs = millis();
  }
  wasPressed = pressed;

  model.setRotation(0.0f, millis() * 0.0005f, 0.0f);  // slow spin (radians)

  scene.render(target);
  display.display();
}
