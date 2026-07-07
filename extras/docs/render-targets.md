# Render targets: the output seam

`TD_RenderTarget` is the library's one abstraction between geometry and
pixels — the 3D analogue of Adafruit_GFX's `drawPixel()`. The renderer runs
the whole pipeline (transform, project, cull, painter sort, Gouraud shade,
scanline setup) and hands the target *shade values*; the target turns them
into pixels for whatever surface it wraps. Two built-ins cover 1-bit and
RGB565 framebuffers, header-only adapters cover anything Adafruit_GFX, and
writing your own takes three methods.

## Writing a custom target: the 3-method minimum

```cpp
#include <ThirdDimension.h>

class MyTarget : public TD_RenderTarget {
 public:
  int16_t width() override { return 128; }
  int16_t height() override { return 64; }
  void plotShade(int16_t x, int16_t y, float shade) override {
    // shade 0 = darkest, 1 = brightest (values outside [0,1] saturate).
    // Coordinates are guaranteed in-bounds — no checks needed.
    if (shade < 0.0f) shade = 0.0f;
    if (shade > 1.0f) shade = 1.0f;
    myBuffer[y][x] = (uint8_t)(shade * 255.0f);
  }
};
```

That is a complete, working target: the base class's `plotShadeSpan`
default loops `plotShade` for you, and `beginFrame` / `setMaterial` /
`endFrame` have no-op defaults.

### Then override plotShadeSpan for speed

Spans are the hot path — one call per scanline segment instead of one per
pixel. Override it to fuse your inner loop and hoist per-row state:

```cpp
void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX,
                   float leftShade, float shadeSlope) override {
  uint8_t *row = &myBuffer[y][0];
  for (int x = xs; x <= xe; ++x) {
    float shade = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope;
    row[x] = ...;
  }
}
```

Compute the shade expression **exactly** as written above — this
floating-point shape is part of the API, pinned by the bit-exact golden
tests. Corollary: `shadeSlope == 0.0f` delivers exactly `leftShade` to every
pixel, so flat-lit faces and saturated fills stay exact.

Optional hooks:

- `beginFrame()` — clear your surface; return false to abort the render.
- `setMaterial(const TD_MaterialCtx &ctx)` — called after the painter sort
  before a face's draw groups: once for the fill, then re-latched to BLACK
  before a SOLID outline pass (cel ink re-latches to BLACK or WHITE per its
  ink style). `ctx` carries the face's `TD_Material`, the render mode
  (`ctx.mode`, informational — it now carries `TD_CEL` = 3 too), the owning
  object's RGB565 `baseColor`, and object/face indices. Latch what you need;
  mono-style targets can ignore it (materials reach them through shade
  encoding).
- `endFrame()` — for deferred output (the mono target's error-diffusion
  methods quantize their recorded frame here).

## The contract

Six guarantees the renderer and target both rely on, enforced by the host
tests driving the real mono target through a recording plot callback.

Renderer guarantees to the target:

- **G1 (in-bounds).** Every coordinate delivered is inside
  `[0, w) × [0, h)` — spans by construction, line pixels through a
  renderer-side bounds funnel. Skip bounds checks in your hot loops.
- **G2 (coverage).** Every pixel covered by a front face is plotted,
  including fully dark ones. Painter overwrite — a near face covering a
  far one — depends on dark pixels being delivered.
- **G3 (endpoint shades).** BLACK-material faces, SOLID-mode outlines, and
  TD_CEL black ink arrive as exactly `0.0`; WHITE faces, SOLID fills,
  WIREFRAME lines, and TD_CEL white ink as `2.0` (saturated past every
  threshold); LIT Gouraud shades are clamped to `[ambient, 1.0]`, and TD_CEL
  band levels arrive within that range as exact slope-0 spans.

Target guarantees to the renderer:

- **T1 (write, don't skip).** A plotted shade-0 pixel is a *write* of the
  darkest value, never a no-op; skipping dark pixels breaks the painter
  overwrite (G2).
- **T2 (endpoints exact).** Shade ≤ 0 produces the darkest output for the
  current material context; shade ≥ 1 the brightest. 1-bit ordered dither
  implementations must additionally be monotone in shade and pure in
  `(x, y, shade, method)`.
- **T3 (containment).** A deferring target (error diffusion) only ever
  emits pixels that were plotted between `beginFrame` and `endFrame`;
  untouched pixels are never written.

## Frame lifecycle

`TD_Scene::render(target)` is defined as **exactly one frame**: one
`beginFrame()`, the geometry, one `endFrame()`. To composite several render
passes into a single frame (two scenes on one display, 3D over a HUD),
bracket manually around the unbracketed pass:

```cpp
target.beginFrame();
sceneA.renderer().renderPass(sceneA, sceneA.camera, target);
sceneB.renderer().renderPass(sceneB, sceneB.camera, target);
target.endFrame();   // diffusion quantizes exactly once
```

Untouched pixels absorb error (T3), so passes compose cleanly. After the
bracket, push the display yourself (`display.display()`, `writePixels`, …) —
the library never talks to hardware.

## Mode × target support matrix

Every cell is defined, tested behavior. `baseColor` is ignored by mono
targets by definition; FLOYD/ATKINSON in SOLID/WIREFRAME modes behave as a
bare 0.5 threshold (provably identical output, since those modes only emit
saturated shades).

| mode / target | TD_MonoTarget | TD_Color565Target |
|---|---|---|
| **TD_SHADED** | Gouraud shades dithered by the active method | Gouraud shade modulates per-object baseColor — no dithering |
| **TD_SOLID** | saturated fill (shade 2.0) + true-edge outlines at 0.0 | flat baseColor fill + outlines in matBlack |
| **TD_WIREFRAME** | front-face loop edges at 2.0 (always lit) | loop edges in per-object baseColor |
| **TD_CEL** | uniform screentone bands under the ordered methods (interiors motion-stable); under diffusion the 1.0 top band is hard-on but mid bands still crawl | N flat color steps of baseColor + ink in the exact matBlack/matWhite palette |
| **materials** | BLACK never speckles; WHITE never drops pixels (dither endpoint guarantees) | BLACK/WHITE fill exact matBlack/matWhite palette colors, light ignored |
| **decals** | draw after all body faces, layer-ordered (envelope in `limits.md`) | same |

A custom `plotShade`-only target gets TD_CEL for free and exactly: band
levels arrive as slope-0 spans, and the default span loop delivers
`leftShade` unchanged to every pixel.

## The built-in targets

### TD_MonoTarget — 1-bit, owns the dithering

Construct with a plot callback and dimensions:

```cpp
void plot(void *user, int16_t x, int16_t y, uint8_t on);  // on=0 IS a write
TD_MonoTarget target(plot, &myDisplay, 128, 64);
target.setDither(TD_DITHER_BLUE16);
```

All ten dither methods live here (`setDither`, `dither`, `ditherName`).
Ordered methods decide inline per span; the two error-diffusion methods
(FLOYD, ATKINSON) record shades all frame and quantize in `endFrame`.
Selecting a diffusion method allocates two w×h float buffers *at
`setDither()` time*, never mid-frame, and returns false (method unchanged)
on allocation failure; `reserve()` pre-allocates them in `setup()`. Budgets
are in `limits.md`. `setAutoClear(false)` skips the `beginFrame` dark-fill
for compositing over existing content.

### TD_Color565Target — RGB565 framebuffer

```cpp
static uint16_t fb[96 * 64];
TD_Color565Target target(fb, 96, 64);
```

Wraps a caller-owned row-major buffer. LIT faces modulate the owning
object's `baseColor` by the interpolated shade (fixed-point per-channel
multiply, exact at both endpoints); BLACK/WHITE faces fill the exact
palette colors set by `setMatColors`. `setBackground` picks the auto-clear
color. Small color panels (e.g. SSD1331) have no read-back, so push the
finished buffer to the panel in one burst after `render()`.

## The GFX adapters (ThirdDimension_GFX.h)

Header-only and opt-in: include `<ThirdDimension_GFX.h>` *instead of*
`<ThirdDimension.h>` and install Adafruit GFX. Nothing in the core
references GFX, so the dependency exists only for sketches that ask for it.

- **TD_GFXMonoTarget** — a `TD_MonoTarget` that plots through
  `gfx.drawPixel(x, y, on ? lit : dark)`. Defaults `lit=1, dark=0` match
  every 1-bit driver's WHITE/BLACK; custom colors give two-color dithering
  on color panels for free. Dimensions are read from the GFX object lazily
  at every `beginFrame`, so constructing before `display.begin()` is safe
  and `setRotation()` is picked up automatically. The right target for
  buffered displays (SSD1306 and friends), where drawPixel is a cheap RAM
  write.
- **TD_GFX565Target** — color through per-pixel `drawPixel`: universal but
  slow, and on an *unbuffered* TFT its per-frame `fillScreen` races the
  raster, so the model blinks. For real color, render off-screen into a
  `GFXcanvas16` and wrap its buffer: `TD_Color565Target(canvas.getBuffer(),
  w, h)`.
