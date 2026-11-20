// SPDX-License-Identifier: MIT
//
// PrimitivesTour — all eight procedural primitives arranged in a ring,
// rotating in place on a slowly orbiting stage. demonstrates mesh
// generation, per-object position/rotation/scale, and framing an animated
// multi-object scene with setTarget and fitRadius.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C,
// display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

// default dimensions: every primitive fits the unit cube.
TD_BoxMesh boxMesh;
TD_SphereMesh sphereMesh;
TD_EllipsoidMesh ellipsoidMesh;  // default: the 2:1 squashed sphere
TD_CylinderMesh cylinderMesh;
TD_ConeMesh coneMesh;
TD_PrismMesh prismMesh;
TD_TorusMesh torusMesh;
TD_PlaneMesh planeMesh(4, 4);  // 4x4 grid so Gouraud gradients show

TD_Object shapes[8] = {
    TD_Object(boxMesh),      TD_Object(sphereMesh), TD_Object(ellipsoidMesh),
    TD_Object(cylinderMesh), TD_Object(coneMesh),   TD_Object(prismMesh),
    TD_Object(torusMesh),    TD_Object(planeMesh),
};

TD_Scene scene;

const float RING_RADIUS = 1.6f;  // world units; shapes sit on this circle

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  for (int i = 0; i < 8; ++i) {
    shapes[i].setScale(0.8f);
    scene.add(shapes[i]);
  }

  scene.camera.setOrbit(0.0f, 0.5f);          // look down onto the ring (radians)
  scene.camera.setTarget(0.0f, 0.0f, 0.0f);   // the ring's center, as the shapes move
  scene.camera.fitRadius(RING_RADIUS + 0.7f); // ring + shape radius, world units
}

void loop() {
  float t = millis() * 0.001f;  // seconds

  for (int i = 0; i < 8; ++i) {
    float a = t * 0.15f + i * (TWO_PI / 8.0f);  // slow orbit, evenly spaced
    shapes[i].setPosition(RING_RADIUS * cosf(a), 0.0f, RING_RADIUS * sinf(a));
    shapes[i].setRotation(0.3f, t * 0.9f + i, 0.0f);  // tumble in place (radians)
  }

  scene.render(target);
  display.display();
}
