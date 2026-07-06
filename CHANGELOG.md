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
