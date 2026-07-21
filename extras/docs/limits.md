# Limits & budgets

Every hard limit and memory budget, with numbers.

## Mesh size: 16-bit indices

| quantity | hard cap | notes |
|---|---|---|
| vertices per mesh | 65 535 | `nverts` is `uint16_t` |
| triangles per mesh | 21 845 | converter cap: the `3·ntris` corner normals must fit uint16 (`ntris` itself holds 65 535) |
| faces per mesh | 65 535 | decal `host` reserves 0xFFFF |
| polygon-loop entries per mesh | 65 535 | `faceVertCount` |

The OBJ converter hard-errors past any cap; the primitive generators fail
soft — `begin()` refuses the allocation and leaves the mesh empty (`valid()`
false, a safe render no-op). Neither ever truncates silently.
**Scene totals may exceed these per-mesh caps**: the renderer sums vertex and
face counts across objects in 32-bit, so twenty 10 000-vertex objects are
fine but one 70 000-vertex mesh is not — split it into multiple meshes.

## Painter's occlusion envelope

Occlusion is the painter's algorithm on per-face mean view depth — no
z-buffer. It is correct for non-interpenetrating objects with reasonable
depth separation and reasonably tessellated faces (face size small relative
to depth differences).

It cannot resolve interpenetrating geometry or occlusion cycles (no draw
order exists), and large faces mis-sort against small nearby objects — the
classic case is a ground plane under a model, whose mean depth wins or loses
as a whole. Fix by subdividing: `TD_PlaneMesh(cols, rows)` exists for
exactly this.

## Decal envelope

Decal faces (per-face attributes with a `host`) draw after **all** body
faces of **all** objects, ordered by `layer`. Within one object this is
exact: no neighboring face of the same object can erase an overhanging
decal. Across objects there is a documented hole — a decal on a far object
draws over a near object's body. **Decals are correct only while their
owning object is not occluded by another object.**

The per-layer depth bias is `(layer + 1) · 0.002 · |objectScale|`. The
`0.002` is in model units; ·|scale| converts it to the sort key's view-depth
units and keeps negative-scale objects from inverting decal order. For the
bias to beat floating-point noise in the depth sums, keep
`cameraDistance / objectScale` below roughly 1000 for decal-bearing objects.

## No near clipping

Faces are never sliced by a near plane. A face with **any** loop vertex
closer than the camera's near-skip distance (`TD_Camera::setNearSkip`,
default 0.1 world units) is skipped whole for that frame — geometry crossing
the threshold *pops* instead of clipping. When models vanish piecemeal as
the camera closes in, `renderer.lastNearSkipCount()` returns the faces
dropped last frame (0 means near-skip is not the cause). Mitigate by keeping
the camera out of the geometry, lowering `setNearSkip`, or tessellating so
faces are smaller than the pop.

## Screen-linear Gouraud

Corner shades interpolate linearly in screen space, not perspective-correct.
Invisible at 128×64; on large, close-up triangles at 320×240 the gradient
bends slightly across the face. Tessellating large faces hides it.
Perspective-correct interpolation is not implemented.

## Added lights

Extra directional lights (`TD_Scene::addLight`) cost per-corner arithmetic
plus one list pointer per light:

- **Per-corner cost:** each added light adds, per lit corner, per frame, one
  3-multiply/2-add dot product, one compare/clamp, two multiplies and one
  add, plus one pointer chase.
- **Zero renderer RAM.** No snapshot array, no per-frame buffer — the whole
  feature costs 4 bytes per `TD_Light` and 4 bytes per scene on 32-bit
  targets.
- **Uncapped, like the object list:** hundreds of lights slow the corner
  loop linearly. Real scenes use 1–3.
- Should the pointer chase ever measure badly, a per-frame flat-array
  snapshot inside the renderer is a bit-invisible pure optimization — same
  floats, same arithmetic, same order. Available, not built.

## Cel shading (TD_CEL)

- **Band count clamps to 2..`TD_CEL_MAX_BANDS`** (8). `setCelBands(0)` and
  `(1)` become 2; anything above 8 becomes 8.
- **Zero added RAM.** Banding adds ~10 B of renderer state and no per-pixel,
  per-face, or per-edge buffers; disabled modes pay one predictable branch
  per span.
- **Per-span split cost:** two band classifications (≤ 7 compares each) plus
  one extra `fillSpan` emission per threshold crossing — typically 0–1 per
  span at 4 bands, at most `bands − 1`. Ink cost equals TD_SOLID's existing
  outline pass.
- **Band count vs dither levels:** a band is only visible if the active
  dither method can distinguish its duty cycle. BAYER2 carries 5 levels, so
  more than ~5 bands collapse under it; at 128×64 keep bands ≤ 6. The two
  diffusion methods render band fields with the 1.0 top band hard-on but
  mid-band interiors still reflow under motion — pair cel with the ordered
  methods for the stable screentone look.
- **Dark-background readability:** the darkest band sits at the ambient
  floor; at the default 0.15 it dithers close to the auto-cleared black
  background. Raise `setAmbient` to ~0.25–0.35 or use `TD_CEL_INK_WHITE`.
- **Ambient near 1.0:** within a few ulps of 1.0 a band threshold can round
  to exactly 1.0f, leaving the top band unreachable — a harmless
  degradation. At ambient ≥ 1 banding disables entirely and TD_CEL renders
  flat-lit.

## Diffusion dither buffers (TD_MonoTarget)

The FLOYD/ATKINSON methods buffer the whole frame as floats: two w×h
buffers, 8 bytes per pixel, allocated at `setDither()`/`reserve()` time
(never mid-frame; on failure `setDither()` returns false and keeps the
previous method).

| resolution | pixels | diffusion buffers |
|---|---|---|
| 128×64 (SSD1306) | 8 192 | 64 KB |
| 128×128 | 16 384 | 128 KB |
| 240×135 (common IPS) | 32 400 | 253 KB |
| 320×240 | 76 800 | 600 KB |

On a 320 KB-class MCU (ESP32), diffusion at 240×135 is marginal and 320×240
does not fit — use the eight ordered methods there (zero buffer, and
temporally stable; diffusion "crawls" under animation). Teensy 4.x and
RP2350-with-PSRAM fit everything.

## Color framebuffers (TD_Color565Target)

RGB565 is 2 bytes per pixel, caller-allocated:

| resolution | framebuffer |
|---|---|
| 96×64 (SSD1331) | 12 KB |
| 128×128 | 32 KB |
| 160×128 | 40 KB |
| 240×135 | 63.3 KB |
| 320×240 | 150 KB |

## Renderer working buffers

Per frame the renderer needs, summed over the *visible* objects of the
largest scene rendered so far:

```
bytes ≈ 12·(total vertices) + 16·(total faces) + 104·(object count)
```

The per-object term is the per-frame transform cache: two 3×3 matrices, a
translation, and the object attributes — 104 B with 32-bit pointers. Worked
examples: a one-object sphere (`TD_SphereMesh(32, 24)` — 738 vertices, 768
faces) ≈ 21 KB; a 4-object, 2 000-vertex, 1 200-face scene ≈ 43 KB. Growth
is exact-fit high-water — buffers grow to the largest scene seen and stay,
with no doubling slack and no per-frame realloc chatter.

### reserve() / releaseBuffers()

```cpp
void setup() {
  if (!scene.renderer().reserve(2000, 1200, 4)) {
    // out of memory HERE, visibly — not mid-animation
  }
}
```

`reserve(maxVerts, maxFaces, maxObjects)` moves all renderer allocation into
`setup()`; `TD_MonoTarget::reserve()` does the same for diffusion buffers.
`releaseBuffers()` frees the renderer's working memory between scenes. A
mid-run grow that fails still fails cleanly: pointers are reassigned only on
success, `render()` returns false with `lastStatus() == TD_ERR_ALLOC`, and
the target is untouched (no partial geometry).

## Reentrancy

Everything is instance state — no globals — which buys exactly this
contract:

- one renderer (one scene) per thread; `render()` is **not** reentrant and
  **not** ISR-safe — never call it from interrupt context;
- two renderers on two threads, two displays, or one scene rendered to two
  targets in sequence are all supported.

## Determinism

Two scopes, don't conflate them:

- **Invariants hold everywhere:** dither endpoints (shade ≤ 0 never lights,
  ≥ 1 always), monotonicity, silhouette coverage, in-bounds plotting,
  deterministic painter order (the sort has full tie-breaks, so frame
  content is a pure function of scene state on every platform and libc).
- **Bit-exact frames are scoped to the pinned CI toolchain:** the
  golden-hash tests compile with a pinned compiler and `-ffp-contract=off`.
  Across arbitrary compilers/MCUs, fused-multiply-add contraction and
  last-ulp libm differences can flip individual dithered pixels. Identical
  binaries produce identical frames; different toolchains produce equivalent
  ones.

## Everything else

- **AVR is not supported** (compile-time error): the library assumes 32-bit,
  hardware-float MCUs — Teensy 4.x, ESP32, STM32F4+, RP2350.
- **Uniform scale only** (`TD_Object::setScale(float)`): non-uniform scale
  would break the normals-transform-by-rotation-only invariant, so the API
  cannot express it.
- **One y-flip, radians, CCW winding** and the other four convention
  commitments: see `conventions.md`.
