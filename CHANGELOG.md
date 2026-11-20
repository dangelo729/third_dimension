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

- **Outline depth test: `TD_Renderer::setOutlineDepthTest(true)` and
  `clearOutlineDepth()`.** Under `TD_OUTLINE`, per-pixel visibility across a whole
  composed frame. The depth buffer holds perspective-correct depth, 1/z, which is exactly
  screen-linear across a planar triangle (screen-linear z is off by up to a world unit
  over a big face seen at a slant), and every fill pixel is depth-tested, not only the
  ink: a pixel is painted where it is nearer than what the buffer holds or ties with it
  within `TD_OUTLINE_DEPTH_TIE` (1e-5, relative; coplanar surfaces drawn from different
  meshes disagree by at most 2.3e-6 over every pose measured), and a tie goes to the
  later draw, so a coplanar layer drawn later — squares on a board, marks on the squares
  — covers the one beneath exactly as the painter's order does. A span crosses the seam
  once per run of passing pixels, at its flat shade with slope 0; a span that passes
  whole makes the call it always made. Ink records its depth too — the far end of the
  line's own tolerance, so a later coplanar layer still ties with it — so a farther
  object drawn in a later pass does not erase a contour where it passes behind. The
  buffer persists across `renderPass()` calls, so passes composed into one frame occlude
  each other, and within one pass a big ground face no longer mis-sorts against a small
  object on it. `render()` clears it for its frame; a caller composing passes calls
  `clearOutlineDepth()` once per frame before the first; switching the test, a new depth
  buffer, or a change of target size clears it too, always in a pass that draws
  something (an empty pass leaves the clear pending and the buffer untouched). Line
  groups hold across passes: once allocated, the group buffer clears with the depth and
  every passing fill stamps its group. `scene.setOutlineDepthTest(on)` passes it
  through. No memory beyond the outline depth buffer (the same 4 B/pixel); on the host
  the fill span loop runs about a third slower with it. Default off. **No golden pins
  moved** — purely additive: with the test off every path draws exactly as before.
  Tested in the outline suite (O10a–g): composed passes occlude each other, fill and ink,
  where by default the later pass draws over; a coplanar layer lands exactly the
  default's pixels, in a later pass and in one pass, and covers an earlier layer's line
  as by default; a square just behind a steep wall, where screen-linear depth is several
  units off, is hidden whole, and one just in front draws as alone; a contour stays ink
  over a fill drawn later behind it; the depth clears on `render()`,
  `clearOutlineDepth()`, a clear left pending by an empty pass, and a target resize
  either way, and an empty pass never touches an unsized or smaller buffer; one
  `render()` draws the pinned K5 frame through the default's exact seam calls and fixes a
  ground-plane mis-sort; and line groups hold across passes.
- **Fill-less objects: `TD_Object::setFill(false)`.** The object fills nothing and
  records no depth, in any mode, so it hides nothing; its edges draw as the mode draws
  lines — classified ink under `TD_OUTLINE`, true-edge outlines under `TD_SOLID` and
  `TD_CEL`, front edges under `TD_WIREFRAME`, nothing under `TD_SHADED`. Under
  `TD_OUTLINE` its back faces get draw records too (counted by `lastDrawCount()`), and the
  ink pass draws their crease and boundary edges — never a silhouette, which draws from
  the front face across it — so the far side of a glass shows through the near, while
  every one of its lines is depth-tested against the frame's fills as any line is: water
  inside the glass hides the far lines it stands in front of. For containers drawn as
  glass. Fill and ink are independent: fill off and ink on is the glass, both off draws
  nothing and still counts for the camera's fit. While it inks, its mesh needs its edge
  table, so `prebuildOutline` and the prepare stage build it as for any object. Default
  `true`. **No golden pins moved** — purely additive: filled objects and the other modes
  take no new draw records, so every scene without a fill-less object sorts and draws
  exactly as before. Tested in the outline suite (O6e): a glass box hides none of a
  sphere inside it; it inks all the filled box inks and the middles of the three edges at
  its far corner, which the filled box hides; a glass sphere inks its contour alone; a
  filled sphere in front of the far corner hides the far edges inside its disc and none
  outside; under `TD_SOLID` and `TD_CEL` it plots only dark true edges, under
  `TD_SHADED` nothing, under `TD_WIREFRAME` the filled box's frame; with its ink off too
  it plots nothing in any mode; and `prebuildOutline` takes it beside a line group.
- **Headless editor export: `tools/editor_bake.js`.** Runs `tools/editor.html`'s own
  export — its DOM-free `td-core` script, lifted out of the page: `parseProjectText` →
  `bake` → `emitHeader` — under the `jsc` shell every Mac ships, or under node. The
  input is a saved project `.json` or an exported `.h`; `--decimate T1,T2,...` sets each
  part's decimate target and `--name` the symbol prefix before the bake. Re-baking an
  exported header with no options reproduces it byte for byte. The header is written
  only after a clean bake.
- **Baked outline edge tables: `TD_MeshEdges` + `setOutlineEdges`.** A static mesh's
  `TD_OUTLINE` edge table can be baked offline by `tools/bake_edges.cpp` into a header
  beside the mesh (TD edges format 1, its own `TD_EDGES_FORMAT` handshake), so it lives
  in flash. `scene.setOutlineEdges(mesh, edges)` hands it in: while its counts match the
  mesh and its crease matches `outlineCrease()`, the renderer reads it in place of a
  built table — no table allocation, and none of the build's scratch (16 B/vertex +
  8 B/loop slot + 12 B/face on top of the 3 B/loop-slot table), which on a 5200-face
  mesh is ~280 KB of heap at once. Otherwise the table builds at runtime as before. The
  tool runs the library's own `TD_EdgeTable::build`, so a baked table is exactly the
  built one. The registration survives `releaseBuffers()` and `invalidateOutline()`;
  a table whose counts differ from the mesh's or whose `opp` entries run past its faces
  is refused. `TD_Mesh` and mesh format 1 are untouched. **No golden pins moved** —
  purely additive. Tested in the outline suite (O8b), including a doctored table that
  proves the ink pass reads the baked one and a crease change that falls back to the
  build.
- **Line groups: `TD_Object::setInkGroup(group)`.** Under `TD_OUTLINE`, objects sharing a
  line group (1..255) draw as one outline: a member's line draws only where it borders the
  background or a fill outside the group, never against the group's own fills. Parts sunk
  into each other — cheeks on a head — show only the contour of their union: no member's
  rim across another (a ball's rim floating over the face it is sunk into), no contour
  through one (the ink test's depth tolerance, several pixels' worth on a grazing
  silhouette face, let a head's jawline ink through a cheek just in front of it), and no
  crease inside any. Against fills outside the group a member's lines depth-test as any
  line does. Since a grouped line can't lean on its own fill, and a fill can reach a pixel
  past its silhouette line, it also looks across the diagonals for the background, so a
  group of one draws exactly as no group. A frame with a group costs **1 B/pixel** more
  (8 KB at 128×64), allocated in the prepare stage only then and freed by
  `releaseBuffers()`. Default 0, no group. **No golden pins moved** — purely additive, and
  a frame without a group runs the old path. Tested in the outline suite (O6d): a ball sunk
  into a bigger one, whose contour and whose rim each ink through by default and not
  grouped; a group of one, dense and coarse, alone and over a box, drawing exactly as no
  group; and an object outside the group inking the same either way.
- **Inkless objects: `TD_Object::setInk(false)`.** The object still fills — under
  `TD_OUTLINE` it still records its depth, so it hides what lies behind it — but none of
  its edges is ever drawn as a line, in any mode: no outline ink, no `TD_SOLID` or
  `TD_CEL` outlines, nothing under `TD_WIREFRAME`. Its mesh needs no edge table, so
  `prebuildOutline` and the prepare stage skip it. For occluders that should hide lines
  without drawing their own — a dark mouth interior behind thin lips, whose rim would
  otherwise ink through within the ink test's one-pixel reach. Default `true`. **No
  golden pins moved** — purely additive. Tested in the outline suite (O6c), including an
  inkless object sharing its mesh with an inked one.
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
