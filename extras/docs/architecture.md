# Architecture

How a frame is made: the pipeline, the data flowing through it, the buffers it
lives in, and the invariants that hold at every stage.

## The big picture

```
sketch            geometry (TD_Mesh: primitives, converted models)
                  TD_Object  (mesh* + position/rotation/scale/color/material)
                  TD_Scene ── camera: TD_Camera (orbit + auto-fit)
                           ── light:  TD_Light  (world-space directional)
                           ── objects: intrusive linked list
                        scene.render(target)
                              │
TD_Renderer       transform → project → cull → ONE global painter sort
                  → per-face material context → Gouraud shade → scanline spans
                              │
                              ▼  plotShadeSpan(...)   ← THE SEAM
TD_RenderTarget   TD_MonoTarget · TD_Color565Target · GFX adapters · yours
                              │
                              ▼
                  the display driver (sketch-side: display.display(),
                  writePixels bursts — the library never talks to hardware)
```

The renderer runs the whole geometry pipeline and hands the target *shade
values*; the target decides pixels. The seam is crossed once per Gouraud
**span**, not per pixel — per-pixel `plotShade` remains only for line drawing
and minimal custom targets. (`TD_CEL` may split one geometric span into
several flat sub-spans at band-threshold crossings; each sub-span still
arrives through one `plotShadeSpan` call.)

## The pipeline, stage by stage

One `scene.render(target)` call performs, in order:

1. **Validate & size.** Cache the target's width/height (non-positive →
   `TD_ERR_NO_TARGET`). Walk the scene list, keeping objects that are visible
   and carry nonzero geometry (an empty/failed-primitive mesh is skipped). Sum
   their vertex and face totals in 32-bit — a scene of u16-indexed meshes can
   exceed u16 totals. Grow the working buffers to fit, or fail with
   `TD_ERR_ALLOC` *before any pixel is touched*, leaving the target untouched.
   Then resolve the camera's framing intent (auto-fit latch, orbit pose)
   against the target dimensions and scene bounds into an eye point, orthonormal
   basis, focal length, and principal point.
2. **Frame bracket.** `target.beginFrame()` — false aborts the render. Built-in
   targets auto-clear here (diffusion mono targets fill their shade field with
   the "untouched" sentinel). The renderer never clears: it writes only pixels
   under front faces, and painter overwrite covers the rest.
3. **Transform.** Per object, precompute `M` (view rotation × object rotation,
   scaled — for points), `N` (object rotation only — for normals), and `T`
   (view rotation × (position − eye)); then stream every vertex through them
   into the shared screen-x / screen-y / view-z buffers at the object's running
   offset.
4. **Project.** `sx = cx + focal·x/z`, `sy = cy − focal·y/z` — the one and only
   y-flip in the library. There is no near-plane clipping: any face with a loop
   vertex closer than the camera's near-skip distance drops whole (sentinel
   depth). See `limits.md`.
5. **Cull & collect.** Per face, a per-*triangle* any-front test — a non-planar
   quad's fan triangles may disagree, and the face draws if any triangle fronts.
   Survivors become draw records carrying mean loop depth. Decal faces (per-face
   attributes with a host face) join a second group, keyed slightly nearer than
   their own depth per stacking layer.
6. **Painter sort.** ONE global `qsort` across all objects: group ascending
   (bodies before decals), depth descending (far first), then object index,
   then face index. The trailing tie-breaks make the frame a pure function of
   the input on every libc, even when depth ties are common (instances of one
   mesh, coplanar tiles).
7. **Shade & draw.** Back to front, per record: `target.setMaterial(ctx)` with
   the face's material, mode, and owning object's base color; then per front
   triangle the three corner shades — BLACK → 0.0, WHITE and SOLID fills → 2.0
   (saturates every dither), LIT under `TD_SHADED` or `TD_CEL` → Gouraud
   `clamp(ambient + (1 − ambient) · max(0, n·L) · intensity)` with the normal
   rotated to world space. Cel keeps corner shades continuous — quantization
   happens per span, after interpolation (stage 8). The scanline fill samples
   pixel centers and is half-open right/bottom, so adjacent triangles neither
   crack nor double-draw.
8. **Spans.** Each scanline segment clamps to the viewport and crosses the seam
   once: `target.plotShadeSpan(y, xs, xe, leftEdgeX, leftShade, shadeSlope)`.
   Under `TD_CEL`, a LIT span is first split at band-threshold crossings into
   flat sub-spans, each re-emitted with equal endpoint shades so the pinned
   slope expression yields exactly `+0.0f` and the target receives the exact
   band level; the quantizer shapes are themselves pinned —
   `td_celThresh(k) = ambient + (1 − ambient) · ((k + 0.5) / (bands − 1))`,
   `td_celLevel(band) = ambient + (1 − ambient) · (band / (bands − 1))` with
   the bottom/top bands returned as exactly `ambient` / `1.0f` by branch, and
   `td_celBand` classifying by comparison against the same `td_celThresh`
   values the splitter solves crossings at. SOLID (and TD_CEL's black ink)
   adds true-edge outlines at shade 0.0; WIREFRAME draws the polygon loops at
   2.0, as does TD_CEL's white ink. Lines are Bresenham, endpoints clipped
   (Liang–Barsky) to a guard band (±4× the target dimensions, in float space)
   before integer conversion, then funneled through a per-pixel bounds check —
   the one place per-pixel checking still exists.
9. **Finish.** `target.endFrame()` — error-diffusion dithers quantize their
   recorded shade field here. `lastDrawCount()` / `lastNearSkipCount()` are
   tallied during culling and drawing above and are final the moment `render()`
   returns. The sketch then pushes the display itself — the library composes
   frames but never owns a bus.

## Frame data flow

```
scene intent ─┐
target w/h  ──┼─→ resolved camera (eye, basis, focal, cx/cy, nearSkip)
scene bounds ─┘
objects ──→ per-object M/N/T ──→ shared sx/sy/sz (vertBase offsets)
                              ──→ per-face mean z (faceZ)
faceZ ──→ cull ──→ DrawRec{z, obj, face, grp} ──→ global sort
sorted records ──→ setMaterial → corner shades → fillTri → fillSpan
fillSpan ──→ plotShadeSpan ──→ target pixels        (endFrame: diffusion)
```

## The working-buffer model

The renderer owns the first three buffers below as instance state (no globals).
The fourth — the diffusion field — belongs to `TD_MonoTarget`, not the renderer,
and is listed only to complete the per-frame memory picture.

| buffer | size | contents | owner |
|---|---|---|---|
| vertex block | 12 B/vertex | screen x, screen y, view z — all objects pooled at per-object offsets | renderer |
| face block | 16 B/face | per-face mean depth + one draw record | renderer |
| object cache | ~104 B/object | per-frame transform state (M, N, T, scale, offsets, color, material) | renderer |
| diffusion field | 8 B/pixel | shade field + error accumulator, only when a diffusion dither is selected | `TD_MonoTarget` |

Growth is **exact-fit, high-water**: buffers grow to the largest scene seen and
stay there (deterministic, no doubling slack). Pointers are reassigned only on a
successful allocation, so a failed grow leaves the previous buffers valid and
the render fails cleanly (stage 1). `TD_Renderer::reserve(maxVerts, maxFaces,
maxObjects)` moves this into `setup()`, where failure is visible;
`releaseBuffers()` returns the memory between scenes. Sizing math and worked
examples are in `limits.md`.

## The invariants

These hold on every platform, enforced by the test suite. The renderer/target
contract (G1–G3 / T1–T3) is specified in `render-targets.md`. Two invariants
belong to the pipeline itself:

- **Frames are deterministic.** The painter sort's trailing tie-breaks make the
  drawn frame a pure function of scene state on every libc (stage 6).
- **Decals draw in order.** Decal faces draw after all body faces of all
  objects, ordered among themselves by stacking layer — a decal is never erased
  by its own object's neighboring faces. (The cross-object envelope of this rule
  is documented in `limits.md`.)

## Mesh format 1 reference

`TD_Mesh` is a non-owning view over geometry arrays that can live in flash
(converter-generated headers) or RAM (procedural primitives). The layout is
**frozen**: any future change is a new struct and a new format number, never a
mutation of this one.

| field | type | contents |
|---|---|---|
| `verts` | `const float (*)[3]` | model-space vertex positions |
| `tris` | `const uint16_t (*)[3]` | fan triangles, face-major order, 0-based |
| `cnorms` | `const float (*)[3]` | 3 per triangle: per-corner unit normals, crease-smoothed, fan-corner order (Gouraud) |
| `faces` | `const uint16_t (*)[4]` | `{ triStart, triCount, loopStart, loopCount }` per original face |
| `faceVerts` | `const uint16_t *` | flattened original polygon loops — the true edges |
| `fattr` | `const TD_FaceAttr *` | optional per-face material/decal attributes; `nullptr` = all LIT, no decals |
| `nverts`, `ntris`, `nfaces` | `uint16_t` | array counts (hard cap 65 535 each) |
| `faceVertCount` | `uint16_t` | length of `faceVerts[]` — enables validation |
| `boundRadius` | `float` | model-space bounding-sphere radius; converter output is 1.0 after normalization; 0.0 is read as "assume 1.0" |

Why both triangles *and* polygon loops: the fan triangles rasterize (convex
faces make fanning safe), while the original loops give the painter true faces
to sort (no fan-diagonal artifacts) and give SOLID/WIREFRAME true edges to
outline. `cnorms` (3 per triangle) dominates mesh RAM; the exact byte formula
and worked sizes are in `limits.md`.

**Version handshake.** `TD_Config.h` defines `TD_MESH_FORMAT 1`. Every generated
model header carries:

```cpp
#if !defined(TD_MESH_FORMAT) || TD_MESH_FORMAT != 1
#error "This model header is TD mesh format 1 — regenerate with a matching tools/obj2mesh.py"
#endif
```

A future move to format 2 turns every stale header into a readable compile error
at its exact include site — never silent garbage geometry. Regenerating with the
matching converter fixes it.

**Validation.** `td_validateMesh()` checks index ranges, face ranges tiling the
triangle and loop tables, `loopCount >= 3`, and decal host indices. Call it once
after hand-building a mesh; the render hot path trusts the mesh completely and
never re-checks.
