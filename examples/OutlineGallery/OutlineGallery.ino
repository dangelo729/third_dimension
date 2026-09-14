// SPDX-License-Identifier: MIT
//
// OutlineGallery — view-dependent feature-line rendering (TD_OUTLINE) on a
// 1-bit display: only the lines that matter are drawn. silhouette edges
// migrate across the surface as objects rotate, crease edges are the
// drawn-on feature lines, boundary edges mark open borders — inked after
// the background-shade fills, each pixel depth-tested against them, so
// hidden lines stay hidden and every contour closes (the pen-and-ink /
// Obra Dinn register; the frame is identical under every dither method,
// since only the endpoint shades are drawn). three demos
// cycle, narrated over Serial: a primitives tour of the classifier, a
// crease-angle sweep on a cylinder (faceted -> feature-lined -> contour
// only), and a two-object hidden-line scene — a cone poking out of a
// sphere reads front-on as a distinct ring inside the contour and fuses
// into one outline as it swings to the side.
//
// requires: Adafruit GFX Library, Adafruit SSD1306. wiring: standard I2C,
// display at address 0x3C.

#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);

TD_BoxMesh boxMesh;
TD_SphereMesh sphereMesh;
TD_CylinderMesh cylinderMesh;
TD_ConeMesh coneMesh;
TD_TorusMesh torusMesh;

TD_Object box(boxMesh);
TD_Object sphere(sphereMesh);
TD_Object cylinder(cylinderMesh);
TD_Object cone(coneMesh);
TD_Object torus(torusMesh);
TD_Scene scene;

// demo 1: one primitive at a time on the turntable — each shows its own
// classification (box: all creases; sphere/torus: pure contour; cylinder:
// rim creases; cone: base-rim ring).
TD_Object *const TOUR[5] = {&box, &sphere, &cylinder, &cone, &torus};
const char *const TOUR_NAMES[5] = {"box", "sphere", "cylinder", "cone", "torus"};

// demo 2: the crease threshold on the spinning cylinder. 15 degrees inks
// every wall edge (faceted), the 60-degree default inks the cap rims only,
// 115 degrees leaves pure contour.
const float CREASE_SWEEP[3] = {0.26f, TD_OUTLINE_CREASE_DEFAULT, 2.0f};
const char *const CREASE_NAMES[3] = {"crease 15 deg (faceted)",
                                     "crease 60 deg (feature lines)",
                                     "crease 115 deg (contour only)"};

const uint32_t TOUR_MS = 3000;    // dwell per primitive (demo 1)
const uint32_t CREASE_MS = 4000;  // dwell per crease step (demo 2)
const uint32_t DEMO_MS = 15000;   // dwell per demo
uint8_t demo = 0, tourIndex = 0, creaseIndex = 0;
uint32_t lastDemoMs = 0, lastStepMs = 0;

void showOnly(TD_Object *keep, TD_Object *also) {
  for (uint8_t i = 0; i < 5; ++i)
    TOUR[i]->setVisible(TOUR[i] == keep || TOUR[i] == also);
}

void enterDemo(uint8_t d) {
  demo = d;
  scene.setOutlineCrease(TD_OUTLINE_CREASE_DEFAULT);
  for (uint8_t i = 0; i < 5; ++i) TOUR[i]->setPosition(0.0f, 0.0f, 0.0f);
  cone.setScale(1.0f);
  if (d == 0) {
    tourIndex = 0;
    showOnly(TOUR[0], nullptr);
    Serial.println(F("demo: primitives tour"));
    Serial.println(TOUR_NAMES[0]);
  } else if (d == 1) {
    creaseIndex = 0;
    showOnly(&cylinder, nullptr);
    scene.setOutlineCrease(CREASE_SWEEP[0]);
    Serial.println(F("demo: crease-angle sweep"));
    Serial.println(CREASE_NAMES[0]);
  } else {
    showOnly(&sphere, &cone);
    cone.setScale(0.5f);
    Serial.println(F("demo: hidden lines (cone through sphere)"));
  }
}

void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  for (uint8_t i = 0; i < 5; ++i) scene.add(*TOUR[i]);
  scene.setRenderMode(TD_OUTLINE);
  scene.camera.fitRadius(1.0f);        // fixed framing across all three demos
  scene.camera.setOrbit(0.0f, 0.35f);  // slight look-down

  // build the edge tables now, where an allocation failure is visible —
  // otherwise they build lazily on the first TD_OUTLINE frame.
  if (!scene.prebuildOutline()) Serial.println(F("outline prebuild failed"));

  enterDemo(0);
}

void loop() {
  uint32_t now = millis();
  if (now - lastDemoMs >= DEMO_MS) {
    enterDemo((demo + 1) % 3);
    lastDemoMs = now;
    lastStepMs = now;
  }
  if (demo == 0 && now - lastStepMs >= TOUR_MS) {
    tourIndex = (tourIndex + 1) % 5;
    showOnly(TOUR[tourIndex], nullptr);
    Serial.println(TOUR_NAMES[tourIndex]);
    lastStepMs = now;
  }
  if (demo == 1 && now - lastStepMs >= CREASE_MS) {
    creaseIndex = (creaseIndex + 1) % 3;
    scene.setOutlineCrease(CREASE_SWEEP[creaseIndex]);  // rebuilds the tables
    Serial.println(CREASE_NAMES[creaseIndex]);
    lastStepMs = now;
  }

  float spin = now * 0.0008f;
  if (demo == 2) {
    // the sphere turns under a cone orbiting from camera-facing to side-on:
    // the base-rim ring inside the contour fuses into the outline.
    sphere.setRotation(0.2f, spin, 0.0f);
    float a = 1.5708f + 0.9f * sinf(now * 0.0005f);  // swing about front-on
    cone.setPosition(0.7f * cosf(a), 0.0f, -0.7f * sinf(a));
    cone.setRotation(0.0f, a, -1.5708f);  // apex along (cos a, 0, -sin a)
  } else {
    TOUR[demo == 1 ? 2 : tourIndex]->setRotation(0.4f, spin, 0.15f);
  }

  scene.render(target);
  display.display();
}
