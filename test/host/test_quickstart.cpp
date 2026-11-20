// SPDX-License-Identifier: MIT
// quick-start suite: the front door, driven the way a sketch drives it. the
// README's sketch (examples/SpinningCube) — a cube in a scene with the
// default camera and light, drawn by scene.render() onto a 128x64 1-bit
// target — and the calls a sketch reaches for next: the auto-fit and
// refit(), setOrbit, fitRadius with setTarget, setFov, and scenes composed
// into one frame with renderPass(). every other suite poses its camera with
// lookAt and setFocalPixels; this one never does.

#include <cmath>
#include <cstring>
#include <initializer_list>

#include "TD_MonoTarget.h"
#include "TD_Primitives.h"
#include "TD_Scene.h"

#include "test_common.h"

// the README's display, 128x64: a framebuffer of its own beside the suites'
// 128x32 fb, written by a plot callback with the same contract.
static const int QW = 128;
static const int QH = 64;
static uint8_t qfb[QH][QW];
static long qOob = 0;

static void qPlot(void *user, int16_t x, int16_t y, uint8_t on) {
  (void)user;
  if (x < 0 || x >= QW || y < 0 || y >= QH) {
    ++qOob;
    return;
  }
  qfb[y][x] = on;
}

static void qClear() { std::memset(qfb, 0, sizeof qfb); }

// lit-pixel census over qfb: count, bounding box, centroid, and the farthest
// lit pixel center from the target center.
struct QStats {
  long count = 0;
  int minx = QW, maxx = -1, miny = QH, maxy = -1;
  double cx = 0, cy = 0, rmax = 0;
};

static QStats qStats() {
  QStats s;
  for (int y = 0; y < QH; ++y)
    for (int x = 0; x < QW; ++x) {
      if (!qfb[y][x]) continue;
      ++s.count;
      if (x < s.minx) s.minx = x;
      if (x > s.maxx) s.maxx = x;
      if (y < s.miny) s.miny = y;
      if (y > s.maxy) s.maxy = y;
      s.cx += x;
      s.cy += y;
      const double dx = x + 0.5 - QW * 0.5, dy = y + 0.5 - QH * 0.5;
      const double r = std::sqrt(dx * dx + dy * dy);
      if (r > s.rmax) s.rmax = r;
    }
  if (s.count) {
    s.cx /= s.count;
    s.cy /= s.count;
  }
  return s;
}

static void qDump(const char *title) {
  std::printf("--- %s ---\n", title);
  for (int y = 0; y < QH; ++y) {
    char row[QW + 1];
    for (int x = 0; x < QW; ++x) row[x] = qfb[y][x] ? '#' : ' ';
    row[QW] = 0;
    std::printf("|%s|\n", row);
  }
}

static bool renderChecked(TD_Scene &scene, TD_RenderTarget &target) {
  qClear();
  const bool ok = scene.render(target);
  CHECK(ok, "scene.render returned false (status %d)", (int)scene.lastStatus());
  return ok;
}

void runQuickstartTests() {
  TD_MonoTarget target(qPlot, nullptr, (int16_t)QW, (int16_t)QH);

  // the auto-fit frames the scene's bound sphere at 0.9 x half the shorter
  // side: no point of the scene projects farther from the center than this.
  const double fitPx = 0.90 * 0.5 * QH;

  // ---- F1. the README's sketch, in every render mode, at any yaw ----
  // the cube draws centered, fully on screen and at the size the auto-fit
  // promises, with no camera or light code at all.
  {
    TD_BoxMesh cubeMesh;
    TD_Object cube(cubeMesh);
    TD_Scene scene;
    scene.add(cube);
    for (int m = 0; m < TD_RENDER_MODE_COUNT; ++m) {
      scene.setRenderMode((TD_RenderMode)m);
      for (int k = 0; k < 8; ++k) {
        cube.setRotation(-0.5f, k * 0.8f, 0.0f);
        if (!renderChecked(scene, target)) continue;
        QStats s = qStats();
        const double bx = (s.minx + s.maxx + 1) * 0.5, by = (s.miny + s.maxy + 1) * 0.5;
        CHECK(s.count > 100, "mode %d yaw %d: the cube drew almost nothing (%ld px)", m, k,
              s.count);
        CHECK(std::fabs(bx - QW * 0.5) < 5.0 && std::fabs(by - QH * 0.5) < 5.0,
              "mode %d yaw %d: the cube is off center (%.1f, %.1f)", m, k, bx, by);
        CHECK(s.rmax <= fitPx + 0.75, "mode %d yaw %d: a pixel lies outside the fit (%.2f > %.2f)",
              m, k, s.rmax, fitPx);
        CHECK(s.maxx - s.minx >= 24 && s.maxy - s.miny >= 24,
              "mode %d yaw %d: the cube is too small (%d x %d)", m, k, s.maxx - s.minx + 1,
              s.maxy - s.miny + 1);
      }
    }
    scene.setRenderMode(TD_SHADED);
    cube.setRotation(-0.5f, 0.8f, 0.0f);
    renderChecked(scene, target);
    qDump("quick start: the cube, shaded, as the README's sketch draws it");
  }

  // ---- F2. the default key light shines from the upper left ----
  // a sphere's lit pixels sit up and to the left of its silhouette.
  {
    TD_SphereMesh sphereMesh;
    TD_Object ball(sphereMesh);
    TD_Scene scene;
    scene.add(ball);
    scene.setAmbient(1.0f);  // every covered pixel lit: the silhouette
    renderChecked(scene, target);
    QStats sil = qStats();
    scene.setAmbient(0.15f);  // the default
    renderChecked(scene, target);
    QStats lit = qStats();
    CHECK(lit.count > 0 && lit.count < sil.count,
          "the shaded sphere must light part of its silhouette (%ld of %ld px)", lit.count,
          sil.count);
    CHECK(lit.cx < sil.cx - 1.0 && lit.cy < sil.cy - 1.0,
          "the default light must light the upper left (lit centroid %.1f, %.1f; silhouette "
          "%.1f, %.1f)",
          lit.cx, lit.cy, sil.cx, sil.cy);
  }

  // ---- F3. the auto-fit latches at the first render; refit() re-latches ----
  {
    TD_BoxMesh cubeMesh;
    TD_Object cube(cubeMesh);
    TD_Scene scene;
    scene.add(cube);
    scene.setRenderMode(TD_SOLID);
    renderChecked(scene, target);
    const double first = qStats().cx;
    cube.setPosition(0.6f, 0.0f, 0.0f);
    renderChecked(scene, target);
    const double moved = qStats().cx;
    scene.camera.refit();
    renderChecked(scene, target);
    const double refit = qStats().cx;
    CHECK(std::fabs(first - QW * 0.5) < 2.0, "the first frame must be centered (cx %.1f)", first);
    CHECK(moved > first + 5.0, "a moved object must move on screen under the latched fit (cx %.1f)",
          moved);
    CHECK(std::fabs(refit - QW * 0.5) < 2.0, "refit() must center the scene again (cx %.1f)",
          refit);
  }

  // ---- F4. setOrbit: azimuth turns the eye about +Y, elevation lifts it ----
  // a plane faces +Y and is culled from below: it draws from above only. a
  // ball on the -Z side of a fixed frame (setTarget + fitRadius) sits in
  // the middle seen from azimuth 0, the eye on -Z, and to the right seen
  // from +pi/2, the eye on -X, to the left from -pi/2.
  {
    TD_PlaneMesh planeMesh(4, 4);
    TD_Object plane(planeMesh);
    TD_Scene scene;
    scene.add(plane);
    scene.camera.setOrbit(0.0f, 0.5f);
    renderChecked(scene, target);
    const long above = qStats().count;
    scene.camera.setOrbit(0.0f, -0.5f);
    renderChecked(scene, target);
    const long below = qStats().count;
    CHECK(above > 200 && below == 0,
          "a plane must show from above and cull from below (%ld px above, %ld below)", above,
          below);

    TD_SphereMesh sphereMesh;
    TD_Object ball(sphereMesh);
    TD_Scene ring;
    ring.add(ball);
    ball.setScale(0.6f);
    ball.setPosition(0.0f, 0.0f, -1.0f);
    ring.camera.setTarget(0.0f, 0.0f, 0.0f);
    ring.camera.fitRadius(1.5f);
    const float az[3] = {0.0f, (float)M_PI / 2, -(float)M_PI / 2};
    double cx[3];
    for (int i = 0; i < 3; ++i) {
      ring.camera.setOrbit(az[i], 0.0f);
      renderChecked(ring, target);
      cx[i] = qStats().cx;
    }
    CHECK(std::fabs(cx[0] - QW * 0.5) < 2.0 && cx[1] > QW * 0.5 + 10.0 &&
              cx[2] < QW * 0.5 - 10.0,
          "orbit azimuth 0, +pi/2, -pi/2 must put the ball middle, right, left (cx %.1f, %.1f, "
          "%.1f)",
          cx[0], cx[1], cx[2]);
  }

  // ---- F5. setFov takes degrees ----
  // the focal is half the target's height over tan(fov / 2), the fov read
  // in degrees, and halving the fov draws the ball about twice as wide.
  {
    TD_SphereMesh sphereMesh;
    TD_Object ball(sphereMesh);
    TD_Scene scene;
    scene.add(ball);
    scene.setAmbient(1.0f);
    int width[2] = {0, 0};
    const float fov[2] = {60.0f, 30.0f};
    for (int i = 0; i < 2; ++i) {
      scene.camera.setFov(fov[i]);
      renderChecked(scene, target);
      const double want = (QH * 0.5) / std::tan(fov[i] * M_PI / 180.0 * 0.5);
      CHECK(std::fabs(scene.camera.focalPixels() - want) < 1e-3,
            "setFov(%.0f): focal %.4f, want %.4f", (double)fov[i],
            (double)scene.camera.focalPixels(), want);
      QStats s = qStats();
      width[i] = s.maxx - s.minx + 1;
    }
    CHECK(width[0] > 10 && width[1] > 1.9 * width[0] && width[1] < 2.4 * width[0],
          "halving the fov must about double the ball (%d px at 60, %d px at 30)", width[0],
          width[1]);
  }

  // ---- F6. scenes composed into one frame with renderPass ----
  // two scenes in one beginFrame..endFrame bracket, each with its own
  // camera, draw exactly the union of the two drawn alone.
  {
    TD_SphereMesh sphereMesh;
    TD_Object left(sphereMesh), right(sphereMesh);
    TD_Scene a, b;
    a.add(left);
    b.add(right);
    left.setPosition(-1.0f, 0.0f, 0.0f);
    right.setPosition(1.0f, 0.0f, 0.0f);
    for (TD_Scene *s : {&a, &b}) {
      s->setAmbient(1.0f);
      s->camera.setTarget(0.0f, 0.0f, 0.0f);
      s->camera.fitRadius(2.0f);
    }
    static uint8_t aloneA[QH][QW], aloneB[QH][QW];
    renderChecked(a, target);
    std::memcpy(aloneA, qfb, sizeof qfb);
    renderChecked(b, target);
    std::memcpy(aloneB, qfb, sizeof qfb);
    qClear();
    CHECK(target.beginFrame(), "beginFrame failed");
    CHECK(a.renderer().renderPass(a, a.camera, target), "the first pass failed");
    CHECK(b.renderer().renderPass(b, b.camera, target), "the second pass failed");
    target.endFrame();
    long litA = 0, litB = 0, overlap = 0, differ = 0;
    for (int y = 0; y < QH; ++y)
      for (int x = 0; x < QW; ++x) {
        litA += aloneA[y][x] != 0;
        litB += aloneB[y][x] != 0;
        overlap += aloneA[y][x] && aloneB[y][x];
        differ += qfb[y][x] != (aloneA[y][x] | aloneB[y][x]);
      }
    CHECK(litA > 100 && litB > 100 && overlap == 0,
          "the two scenes must draw apart (%ld and %ld px, %ld overlapping)", litA, litB, overlap);
    CHECK(differ == 0, "the composed frame must be the union of the two (%ld px differ)", differ);
  }

  CHECK(qOob == 0, "quick-start suite plotted out of bounds %ld times", qOob);
}
