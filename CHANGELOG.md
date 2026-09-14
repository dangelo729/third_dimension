# Changelog

All notable changes to ThirdDimension are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html) with
two domain rules on top: any change visible in the golden-pinned render output is at
least a MINOR bump (with an entry here explaining why the pins moved), and any change to
the `TD_Mesh` layout is a new mesh format number plus a MAJOR bump. During 0.x, MINOR
releases may break API; breaks are called out under a "Breaking" heading.

## [Unreleased]

### Added

- **Ellipsoid primitive: `TD_EllipsoidMesh(slices, stacks, rx, ry, rz)`.** The
  non-uniform sphere: UV-sphere topology (identical counts and memory to
  `TD_SphereMesh` at equal tessellation) stretched to independent semi-axes, with the
  scale baked into the mesh so `TD_Object::setScale` stays uniform and normals never
  renormalize in the render loop. Corner normals are the exact analytic
  implicit-surface gradient `normalize(x/rx², y/ry², z/rz²)` — not the scaled sphere
  normal — so Gouraud highlights sit where the curvature actually puts them;
  `rx = ry = rz` reproduces the sphere vertex-for-vertex (tested).
  `boundRadius = max(rx, ry, rz)`, exact for the analytic surface. Default
  `(0.5, 0.25, 0.5)` is a 2:1 squashed sphere inside the unit cube. **No existing
  golden pins moved** — purely additive: a new generator with its own normal helper,
  no shared expression changed (verified by running the full suite with only the new
  pin unset). New pin: the ellipsoid entry in `GOLDEN_MESH`. `PrimitivesTour` becomes
  an eight-shape tour.
- **Outline (feature-line) rendering: the `TD_OUTLINE` render mode.** Draws only the
  lines that matter — silhouette edges (view-dependent: the classifier compares each
  edge's neighbor against the per-frame facing bits, so the lines migrate as objects
  rotate), crease edges (dihedrals sharper than `setOutlineCrease`, default 60° for
  converter parity), and boundary edges (open-mesh borders) — inked at exactly 2.0
  over faces filled at exactly 0.0. The fills record view depth per pixel (a **4 B/pixel**
  depth buffer, sized from the target in the prepare stage, freed by
  `releaseBuffers()`); the ink pass runs after every fill has landed and draws each line
  pixel only where the edge's depth passes against the nearest fill at that pixel or
  one of its four neighbors, within `TD_OUTLINE_DEPTH_TOL_PX` pixels' worth of the
  face's steepest depth slope. So hidden lines stay hidden, and a contour is one closed
  line on any mesh: inking inside the painter loop would let the next face's
  pixel-center fill land on the line pixels its neighbor just drew, which on a mesh
  with sub-pixel facets reads as a dotted silhouette. `TD_MATERIAL_WHITE` faces fill at
  2.0 instead (solid-white opt-in); decal faces
  are skipped whole (their unshared loop edges would ink as permanent boundary
  rectangles). Edge-adjacency tables (new internal module `td_edge_table`: position-
  welded pairing, Newell-normal crease bake, 3 B per loop entry) are renderer-managed —
  built automatically in the prepare stage under the allocate-before-any-pixel
  contract, cached per mesh, with `scene.prebuildOutline()` for setup-time visibility
  and `renderer.invalidateOutline(mesh)` for topology changes. Because only the two
  endpoint shades ever cross the seam, the rendered frame is **identical under all ten
  dither methods** — pinned as the mode's own invariant. **No existing golden pins
  moved** — everything new is mode-gated (the facing-bit memset/stores and the draw
  branch run only under `TD_OUTLINE`; no shared floating-point expression or draw-set
  changed), the same mechanism as the addLight/TD_CEL precedents, verified by the
  opt-out identity tests on both CI OSes. New pins: `GOLDEN_OUTLINE_FRAME` (one hash
  asserted across all ten dithers), `GOLDEN_OUTLINE_565`, and the `GOLDEN_TABLE_BOX` /
  `GOLDEN_TABLE_CONE` edge-table structure hashes, and a contour-closure test on a
  dense sphere (a border flood must never reach the rim). Renderer sizing grows 16 → 17 B/face
  and ~104 → ~108 B/object. Custom targets note: `TD_MaterialCtx.mode` (informational)
  may now carry the value 4, and outline latches BLACK (or WHITE) for fills, WHITE for
  ink. New example: `OutlineGallery`.
- **Multiple directional lights: `TD_Scene::addLight` / `removeLight` /
  `firstExtraLight`.** Extra user-owned `TD_Light` objects join the scene intrusively —
  allocation-free, uncapped, one scene at a time, the same membership contract as
  `TD_Object` — while `scene.light` remains the always-shading key light. Per lit
  corner the shade sums the key light **first**, then the added lights in add order
  (the summation order is documented API), with one clamp at 1.0; cel quantizes the
  summed result. **No existing golden pins moved** — with no lights added the
  added-light loop is empty (one null compare). New pins: `GOLDEN_TWO_LIGHT` and
  `GOLDEN_TWO_LIGHT_CEL`. `sizeof(TD_Light)` grows 16 → 20 bytes on 32-bit targets (one
  intrusive pointer). The `SolarSystem` example gains a rim light.
- **Cel (toon) shading: the `TD_CEL` render mode.** Gouraud lighting quantized *after*
  interpolation into 2–8 flat bands spanning `[ambient, 1.0]` (`setCelBands`, default 4),
  plus switchable true-edge outline ink (`setCelInk`: `TD_CEL_INK_BLACK` default /
  `WHITE` / `NONE`). The renderer splits each Gouraud span at band-threshold crossings
  into flat sub-spans re-emitted with `shadeSlope == 0.0f`, so **every** target — all ten
  mono dithers, RGB565, GFX adapters, and custom `plotShade`-only targets — receives
  exact band levels with zero target changes. On 1-bit displays each band renders as a
  uniform screen-locked dither tile (the screentone look; band interiors are temporally
  stable under the ordered methods), on RGB565 as N exact flat color steps. The pure
  quantizers ship as pinned API surface (`td_celBand`, `td_celLevel`, `td_celThresh`),
  with the band ceiling at `TD_CEL_MAX_BANDS` (8). **No existing golden pins moved** —
  cel is additive, and disabled paths evaluate byte-identical expressions; new pins cover
  the quantizer signature, cel golden frames for all ten dither methods, band/ink sweeps,
  and the library's first color-target frame hash. Custom targets note:
  `TD_MaterialCtx.mode` (informational) may now carry the value 3, and cel ink re-latches
  the material context to BLACK or WHITE per face. New example: `CelGallery`.
- **Full implementation of the locked API — the library renders.** `TD_Renderer` runs the
  complete multi-object pipeline (transform → project → per-triangle cull → one global
  painter sort with deterministic tie-breaks → Gouraud shade → span rasterization), plus
  decal grouping, whole-face near-skip, guard-band line clipping, and the
  allocate-before-any-pixel failure contract. Shipped with it: `TD_Camera` framing-intent
  resolution (auto-fit, orbit, look-at, FOV, raw focal); `TD_MonoTarget` with all 10
  dither methods (the 8 ordered methods fused in the span loop, FLOYD/ATKINSON as a
  diffusion post-pass); `TD_Color565Target` with per-object color and exact BLACK/WHITE
  palette materials; all 7 procedural primitives through the single-allocation mesh
  builder; and `td_validateMesh`.
- Golden verification: the pre-existing bit-exact golden pins (per-method dither tile
  signatures and full-frame render hashes carried over from the proven source engine)
  pass unchanged through the new library under the legacy mapping — the port is
  pixel-identical. The committed host suite (the acceptance gate) builds at `-O1` with
  `-ffp-contract=off`; that flag is load-bearing, since FMA contraction shifts last-ulp
  results and would move the pins.
- Primitive geometry golden hashes pinned (P8) from the first verified generator build;
  one multi-object occlusion threshold recalibrated against the recomputed geometry.

### Fixed

- Guard-band line endpoints are now clipped **parametrically** (Liang–Barsky against the
  band rectangle) instead of per-axis clamped — per-axis clamping bent the visible
  portion of WIREFRAME/SOLID edges whose projections left the band. In-band endpoints are
  untouched, so the golden pins are unaffected.
- `TD_Color565Target::beginFrame()` fails on a null framebuffer (renderer reports
  `TD_ERR_NO_TARGET`) instead of letting spans write through a null pointer.
- Failed-render atomicity is now real: `render()` validates, sizes buffers, and resolves
  the camera **before** opening the `beginFrame`/`endFrame` bracket, so a failed render
  no longer auto-clears the target.
- `render()`/`renderPass()` take the camera by non-const reference (resolving framing
  intent mutates its latched state; the previous `const_cast` invited undefined behavior
  on a genuinely const camera).
- Decal depth bias multiplies the scale **magnitude**, so mirrored (negative-scale)
  objects keep their decal layer order.
- `TD_Light`'s default direction is built by its own `setDirection(0.7f, 0.45f)` instead
  of a hand-typed constant that was off by 6e-4 and not unit length.
- CI: removed the skeleton-era `continue-on-error` from the host-test and
  example-compile jobs — both gates are now armed.
- Docs: working-buffer budget corrected to ~104 B/object everywhere (was ~60);
  `setDistance` and `TD_ERR_UNSUPPORTED` comments now state actual behavior; the
  multi-pass compositing snippet in `render-targets.md` compiles as written.

### Earlier (repository skeleton)

- The public API is locked as headers under `src/`:
  scene types (`TD_Scene`, `TD_Object`, `TD_Camera`, `TD_Light`), the engine
  (`TD_Renderer`), the render-target seam (`TD_RenderTarget`) with 1-bit
  (`TD_MonoTarget`) and RGB565 (`TD_Color565Target`) targets, the standalone dither
  module (`TD_Dither`, 10 methods, frozen order), the frozen mesh format 1 (`TD_Mesh`),
  seven procedural primitives, and the opt-in header-only Adafruit_GFX adapters
  (`ThirdDimension_GFX.h`).
- Packaging: `library.properties` / `library.json` manifests, `keywords.txt`, MIT
  license, CI workflows (host tests, example-compile matrix for Teensy 4.1 / ESP32 /
  RP2350, strict lint), and a PlatformIO publish workflow.
- Tooling scaffolding: OBJ-to-header converter and blue-noise generator under `tools/`,
  host test harness under `test/host/`.
