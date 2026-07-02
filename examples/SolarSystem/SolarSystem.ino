// SPDX-License-Identifier: MIT
//
// SolarSystem — multi-object occlusion demo: a self-lit sun and two planets
// orbiting it, all three objects sharing one generated sphere mesh. the
// global painter sort keeps occlusion correct as planets pass behind and in
// front of the sun — add() order never matters.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C,
// display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

TD_SphereMesh sphereMesh(12, 8);  // one mesh, shared by all three objects
TD_Object sun(sphereMesh), earth(sphereMesh), mars(sphereMesh);
TD_Scene scene;

const float EARTH_ORBIT = 1.8f;  // world units
const float MARS_ORBIT = 2.8f;

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // white material ignores the light: always fully lit, reads as glowing.
  sun.setMaterial(TD_MATERIAL_WHITE);

  earth.setScale(0.55f);
  mars.setScale(0.40f);

  scene.add(sun);
  scene.add(earth);  // add order never matters — the depth sort is global
  scene.add(mars);

  scene.setAmbient(0.10f);                // deep-space shadows
  scene.light.setDirection(0.4f, 0.3f);   // one directional key light (az, el, radians)
  scene.camera.setOrbit(0.0f, 0.35f);     // look down onto the orbital plane (radians)
  scene.camera.fitRadius(MARS_ORBIT + 0.3f);  // frame the outer orbit, world units
}

void loop() {
  float t = millis() * 0.001f;  // seconds

  earth.setPosition(EARTH_ORBIT * cosf(t * 0.50f), 0.0f, EARTH_ORBIT * sinf(t * 0.50f));
  earth.setRotation(0.0f, t * 2.0f, 0.0f);  // day spin (radians)

  mars.setPosition(MARS_ORBIT * cosf(t * 0.31f + 2.0f), 0.0f,
                   MARS_ORBIT * sinf(t * 0.31f + 2.0f));
  mars.setRotation(0.0f, t * 1.6f, 0.0f);

  scene.render(target);
  display.display();
}
