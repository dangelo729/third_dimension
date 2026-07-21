# ThirdDimension

[![CI](https://github.com/GITHUB_USER/ThirdDimension/actions/workflows/ci.yml/badge.svg)](https://github.com/GITHUB_USER/ThirdDimension/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Arduino Lint](https://img.shields.io/badge/arduino--lint-strict-teal.svg)](https://github.com/GITHUB_USER/ThirdDimension/actions/workflows/ci.yml)

**Adafruit_GFX for 3D** — a tiny scene-based 3D renderer for 32-bit microcontrollers.
Spinning logos, model viewers, dashboards, and toys on small OLED/TFT displays, without
porting a PC engine.

> **Status: pre-release, feature-complete on host (v0.1.0).** The full pipeline is
> implemented and the complete host suite passes with bit-exact golden pins. Still ahead
> of registry publication: on-hardware validation and measured performance.

---

## What it is / what it is not

**It is:**

- A scene API — `TD_Scene` + `TD_Object` + `TD_Camera` (orbit, look-at, auto-fit) +
  directional `TD_Light`s (a default key light; `scene.addLight(...)` for more) — so
  the minimal sketch is ~20 lines with no camera, light, or focal-length code.
- A proven pipeline: transform → project → per-triangle back-face cull → one global
  painter sort → Gouraud shade → span rasterization.
- Four render modes (`TD_SHADED`, `TD_SOLID`, `TD_WIREFRAME`, and `TD_CEL` — banded
  cel/toon shading with switchable outline ink) on pluggable render targets: 1-bit (with
  **10 switchable dither methods**), RGB565, Adafruit_GFX, or your own subclass in 3
  overrides — see the table below.
- A complete asset pipeline: any OBJ (PicoCAD included) → `tools/obj2mesh.py` →
  flash-resident mesh header. Plus 7 procedural primitives.
- Host-tested, with CI-pinned bit-exact golden frame hashes.

**It is not:**

- A z-buffered engine — occlusion is the painter's algorithm on per-face mean depth
  (documented envelope: non-interpenetrating objects with depth separation).
- Textured, near-clipped, or perspective-correct (see the roadmap below).
- A display driver — the library never touches hardware; you push the pixels exactly as
  you do today (`display.display()`, `writePixels`, ...).
- For AVR. A 32-bit MCU with hardware float is the floor (Teensy 4.x, ESP32, STM32F4+,
  RP2350); the umbrella header enforces it with a compile-time `#error`.

---

## Quick start

```cpp
#include <Adafruit_SSD1306.h>
#include <ThirdDimension_GFX.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TD_GFXMonoTarget target(display);
TD_BoxMesh       cubeMesh;
TD_Object        cube(cubeMesh);
TD_Scene         scene;

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  scene.add(cube);
}
void loop() {
  cube.setRotation(0.5f, millis() * 0.0008f, 0.0f);
  scene.render(target);      // transform, sort, shade, dither, plot
  display.display();
}
```

Everything above is automatic: the camera auto-fits on the first frame, a default key
light is aimed, and the target auto-clears in `beginFrame` (opt out with
`setAutoClear(false)` for HUD compositing). No `begin()` on any `TD_` object, and all
angles are radians.

---

## Installation

- **Arduino IDE:** Library Manager → search *ThirdDimension* (after registry
  publication). Until then: clone this repo into your sketchbook's `libraries/` folder.
- **arduino-cli:** `arduino-cli lib install ThirdDimension` (after publication).
- **PlatformIO:** `lib_deps = GITHUB_USER/ThirdDimension` (or the registry package once
  published).

Include exactly one umbrella header per sketch:

- `<ThirdDimension.h>` — the dependency-free core (mono callback + RGB565 framebuffer
  targets).
- `<ThirdDimension_GFX.h>` — the core **plus** the Adafruit_GFX adapters; requires the
  *Adafruit GFX Library* to be installed (the library itself declares no hard dependency
  on it).

---

## Core concepts

```
mesh (flash header or primitive)
  └─ TD_Object   position · rotation · scale · color · material
       └─ TD_Scene   camera (orbit / auto-fit) + key light (+ added lights) + object list
            └─ TD_Renderer   transform → project → cull → painter sort → Gouraud spans
                 └─ TD_RenderTarget   plotShadeSpan()  ← the seam, crossed once per (sub-)span
                      └─ your display push (display.display(), writePixels, ...)
```

- **Meshes** are non-owning views (`TD_Mesh`, frozen format 1) over arrays in flash
  (converter output) or RAM (primitives). True polygon loops are kept, so outlines and
  depth sorting use real faces, never triangulation diagonals.
- **Objects** are user-owned and intrusively linked — `scene.add(obj)` allocates nothing,
  and many objects can share one mesh.
- **The camera stores intent** (fit-scene, fit-radius, a FOV, or a raw focal) and resolves
  it against the target's dimensions at render time.
- **Render modes** are set on the scene; the target decides pixels. `TD_SHADED` is
  dithered on 1-bit targets and modulates per-object color on RGB565. `TD_CEL` quantizes
  lit fills to flat bands after interpolation — the renderer may split a geometric span
  into several flat sub-spans, each crossing the seam once.
- **`render()` returns `bool`** so sketches read forward; a failure's reason is in
  `scene.lastStatus()` (`TD_ERR_ALLOC`, `TD_ERR_BAD_CAMERA`, ...).
- **Deeper reference.** The full pipeline walk-through is in
  [`extras/docs/architecture.md`](extras/docs/architecture.md); the coordinate frame,
  winding, and rotation order (and the assumptions that bite if you guess) are in
  [`extras/docs/conventions.md`](extras/docs/conventions.md).

## Render targets

| Target | Header | Output | Notes |
|---|---|---|---|
| `TD_MonoTarget` | `ThirdDimension.h` | 1-bit via pixel callback | owns the dither method and diffusion buffers |
| `TD_Color565Target` | `ThirdDimension.h` | RGB565 into a caller-owned framebuffer | shade modulates per-object base color; exact BLACK/WHITE palette |
| `TD_GFXMonoTarget` | `ThirdDimension_GFX.h` | any Adafruit_GFX display, dithered | reads `width()/height()` lazily each frame; custom lit/dark colors |
| `TD_GFX565Target` | `ThirdDimension_GFX.h` | any Adafruit_GFX color display via `drawPixel` | universal but slow — prefer `GFXcanvas16` + `TD_Color565Target` |
| your own | subclass `TD_RenderTarget` | anything | 3 required overrides: `width`, `height`, `plotShade` |

The renderer/target contract (bounds, coverage, endpoint shades) is API law and is
documented in [`extras/docs/render-targets.md`](extras/docs/render-targets.md).

## Dithering gallery

All ten methods on `TD_MonoTarget`, switchable at runtime with
`target.setDither(...)` — order is frozen, so `(i + 1) % TD_DITHER_COUNT` cycling is
stable forever:

| Method | Character |
|---|---|
| `TD_DITHER_BAYER8` | classic 8×8 ordered matrix, 65 levels — the default |
| `TD_DITHER_BAYER4` | 4×4, 17 levels: chunky retro crosshatch |
| `TD_DITHER_BAYER2` | 2×2, 5 levels: the stylistic extreme |
| `TD_DITHER_BLUE16` | 16×16 blue noise: organic, clump-free dots |
| `TD_DITHER_CLUSTER8` | clustered-dot halftone: newspaper dots |
| `TD_DITHER_IGN` | interleaved gradient noise: fine film-grain |
| `TD_DITHER_SCANLINE` | horizontal stripes whose duty cycle tracks shade |
| `TD_DITHER_MONO` | no dithering: hard on/off threshold at 0.5 |
| `TD_DITHER_FLOYD` | Floyd–Steinberg error diffusion: smoothest gradients (frame post-pass) |
| `TD_DITHER_ATKINSON` | Atkinson diffusion: the crushed classic-Mac look |

The eight ordered methods are pure per-pixel functions (animation-stable); the two
diffusion methods look smoother but crawl under motion and need an 8 B/pixel buffer.

## Cel shading

`TD_CEL` quantizes the Gouraud lighting into flat bands — after interpolation, so band
boundaries curve smoothly across faces — and outlines true face edges in switchable ink.
On a 1-bit display every band becomes one uniform, screen-locked dither pattern: the
manga-screentone / classic-Mac look, with band interiors bit-stable under motion (pair it
with the ordered dither methods; diffusion reflows band interiors). On RGB565 the same
mechanism yields exact N-step flat color — the classic toon ramp.

```cpp
scene.setAmbient(0.30f);        // lift the darkest band clear of the black background
scene.setRenderMode(TD_CEL);    // 4 bands, black ink — tune below if wanted
scene.setCelBands(3);           // 2..8 uniform bands spanning [ambient, 1.0]
scene.setCelInk(TD_CEL_INK_WHITE);  // black / white / none; white pops on dark OLEDs
```

The darkest band sits at the ambient floor, so on the default dark background raise
`setAmbient` to ~0.25–0.35 (or use white ink) to keep the shadow side readable. Outlines
are all true edges — the hand-drawn look — not silhouette-only; `TD_CEL_INK_NONE` gives
pure screentone bands. See `CelGallery` and [`extras/docs/limits.md`](extras/docs/limits.md)
for band-count vs dither-method guidance.

## More lights

```cpp
TD_Light fill;                     // user-owned, like objects

void setup() {
  fill.setDirection(-2.4f, 0.3f);  // az, el (radians): roughly opposite the key light
  fill.setIntensity(0.5f);
  scene.addLight(fill);
}
```

Each added light costs a little per lit corner every frame, so two or three is the sweet
spot. `scene.light` always shades; added lights sum after it in add order — silence any
light with `setIntensity(0)`. Fills and rims read well at 0.3–0.8 intensity.

## Bring in your own models

```
python3 tools/obj2mesh.py model.obj -o Model.h --name model
```

```cpp
#include "Model.h"
TD_Object obj(model_mesh);
```

The converter fan-triangulates n-gons (keeping true face loops), centers and normalizes
to a unit bounding sphere, computes crease-smoothed corner normals (default 60°), checks
winding via signed volume (hinting `setCullSign(-1)` if reversed), and emits a
self-validating header that `#error`s against a mismatched mesh format.

**PicoCAD workflow:** PicoCAD's OBJ export is already Y-up and CCW-outward — convert with
no flags (export OBJ, not the raw `.txt`). Flags and pipeline internals are in
[`extras/docs/asset-pipeline.md`](extras/docs/asset-pipeline.md).

## Examples

| Sketch | Shows |
|---|---|
| `SpinningCube` | the minimal sketch above |
| `PrimitivesTour` | all 7 primitives (box, sphere, cylinder, cone, prism, torus, plane) on a turntable |
| `SolarSystem` | multi-object scenes and painter occlusion |
| `ImportedModel` | a converted OBJ; a button cycles dither methods |
| `DitherGallery` | auto-cycles all 10 methods, names over Serial |
| `CelGallery` | cel shading: band counts, ink styles, and the ordered dithers on a torus |
| `ColorCube_SSD1331` | the RGB565 framebuffer path with a burst push |
| `CustomTarget_NoGFX` | a raw pixel-callback target with zero dependencies |

## Memory model

- Renderer working buffers: **12 B/vertex + 16 B/face + ~104 B/object**, summed over the
  visible scene; exact-fit high-water reallocation, grown only on success. A failed grow
  leaves the target untouched and `render()` returns `false` with `TD_ERR_ALLOC`.
  `reserve()` moves allocation into `setup()`; `releaseBuffers()` gives it back.
- Diffusion dithers (`FLOYD`/`ATKINSON`): **8 B/pixel** (64 KB at 128×64), allocated at
  `setDither()`/`reserve()`, never mid-frame.
- Procedural primitives: one `malloc` per mesh (default box 696 B, default sphere ≈
  12.8 KB); a failed alloc leaves a valid empty mesh that renders as a safe no-op — check
  `valid()`.
- The renderer writes only silhouette-covered pixels — backgrounds and HUDs survive with
  auto-clear off.

Budgets, worked examples, and per-resolution tables are in
[`extras/docs/limits.md`](extras/docs/limits.md).

## Testing & CI

The core has zero Arduino dependencies, so the whole suite runs on a host PC:

```
clang++ -std=c++17 -O1 -Wall -Wextra -Werror -ffp-contract=off \
  -Isrc src/*.cpp src/internal/*.cpp test/host/*.cpp -o test/host/td_host_tests \
  && ./test/host/td_host_tests
```

The strategy is layered: portable invariant tests (dither endpoints and monotonicity,
silhouette equality across methods, light response, painter ordering, fuzzing) run
anywhere, on host or on-device; bit-exact golden hashes (FNV-1a tile
and frame pins for all 10 dither methods, plus mesh-structure pins for every primitive)
are CI-pinned and require `-ffp-contract=off`. CI also compiles every example for Teensy
4.1, ESP32, and RP2350, and lints the manifests strictly.

## Roadmap (v1 non-goals)

Deliberately out of scope for v1, in rough roadmap order: z-buffer target · real
near-plane clipping (v1 skips whole faces at `nearSkip`) · textures/UVs · point lights ·
quaternions · non-uniform scale · perspective-correct interpolation · animation clips ·
packed 1-bit mono framebuffer target · compressed diffusion buffers · arena/user-buffer
allocation · runtime OBJ loading. AVR support: never.

## Versioning

SemVer, with two domain rules: any change visible in the golden-pinned render output is
**at least a MINOR** bump (with a CHANGELOG entry explaining why the pins moved), and any
`TD_Mesh` layout change is a **new format number and a MAJOR** bump (generated headers
handshake the format at compile time). During 0.x, MINOR releases may break API, called
out under a "Breaking" heading.

## License & acknowledgments

MIT — see [LICENSE](LICENSE).

The scene/target architecture follows **Adafruit_GFX**: one small abstraction, legible
limits, examples as the front door. No Adafruit code is copied; the GFX adapters are
optional and header-only. Dither lineage: Bayer, Floyd–Steinberg, Atkinson, and Jimenez
(interleaved gradient noise).
