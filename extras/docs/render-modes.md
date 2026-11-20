# Render modes, dithering and lights

How a scene can look: the five render modes, the ten 1-bit dither methods,
and the lights. Costs and envelopes are in `limits.md`; what each mode sends
to each kind of target is in `render-targets.md`.

```cpp
scene.setRenderMode(TD_CEL);   // TD_SHADED (default), TD_SOLID, TD_WIREFRAME, TD_CEL, TD_OUTLINE
```

| mode | draws |
|---|---|
| `TD_SHADED` | Gouraud-lit fills: dithered on a 1-bit target, the object's color shaded on RGB565 |
| `TD_SOLID` | flat fills, each face outlined dark along its true edges |
| `TD_WIREFRAME` | the polygon edges of front faces |
| `TD_CEL` | lighting quantized into flat bands, with switchable outline ink |
| `TD_OUTLINE` | silhouette, crease and boundary lines only, hidden lines removed |

## Dithering

`TD_MonoTarget` (and `TD_GFXMonoTarget`) turns shades into 1-bit pixels with
one of ten methods, switchable at any time with `target.setDither(...)`. The
order is frozen, so cycling with `(i + 1) % TD_DITHER_COUNT` is stable.

| method | character |
|---|---|
| `TD_DITHER_BAYER8` | 8×8 ordered matrix, 65 levels — the default |
| `TD_DITHER_BAYER4` | 4×4, 17 levels: chunky retro crosshatch |
| `TD_DITHER_BAYER2` | 2×2, 5 levels: the stylistic extreme |
| `TD_DITHER_BLUE16` | 16×16 blue noise: organic, clump-free dots |
| `TD_DITHER_CLUSTER8` | clustered-dot halftone: newspaper dots |
| `TD_DITHER_IGN` | interleaved gradient noise: fine film grain |
| `TD_DITHER_SCANLINE` | horizontal stripes whose duty cycle tracks shade |
| `TD_DITHER_MONO` | no dithering: a hard threshold at 0.5 |
| `TD_DITHER_FLOYD` | Floyd–Steinberg error diffusion: the smoothest gradients |
| `TD_DITHER_ATKINSON` | Atkinson diffusion: the crushed classic-Mac look |

The eight ordered methods are pure per-pixel functions, so a still surface
stays still under animation. The two diffusion methods run over the whole
frame in `endFrame()`; they look smoother but crawl under motion, and
selecting one allocates an 8 B/pixel buffer (`setDither` returns false and
keeps the old method if it can't).

## Cel shading

`TD_CEL` quantizes the Gouraud lighting into flat bands after
interpolation, so band boundaries curve smoothly across faces, and outlines
true face edges in switchable ink. On a 1-bit display each band becomes one
uniform, screen-locked dither pattern — screentone — whose interior holds
still under motion with the ordered methods; on RGB565 it is N flat steps of
the object's color.

```cpp
scene.setAmbient(0.30f);            // lift the darkest band off the black background
scene.setRenderMode(TD_CEL);
scene.setCelBands(3);               // 2..8 bands spanning [ambient, 1.0]; default 4
scene.setCelInk(TD_CEL_INK_WHITE);  // TD_CEL_INK_BLACK (default), _WHITE or _NONE
```

The darkest band sits at the ambient floor, so on a dark background raise
`setAmbient` to about 0.25–0.35, or use white ink. Band count against dither
method is in `limits.md`; `CelGallery` cycles all three knobs.

## Outline rendering

`TD_OUTLINE` draws only the lines that matter: **silhouette** edges, where
the surface turns away from the camera (they move as the object turns);
**crease** edges, where faces meet at a dihedral sharper than a threshold;
and **boundary** edges of open meshes. Faces fill at the background shade
and record their depth; after every fill, the ink pass draws only the line
pixels the fills leave in front, so hidden lines stay hidden and a contour
is one closed line however fine the mesh.

```cpp
scene.setRenderMode(TD_OUTLINE);       // white ink (shade 2.0) over black fills
scene.setOutlineCrease(radians(45));   // crease threshold; default 60°, the converter's
scene.prebuildOutline();               // optional: build edge tables in setup()
```

Each mesh's edge table is built and cached by the renderer the first time
it is drawn this way; `prebuildOutline()` does it in `setup()`, where an
allocation failure is visible. A mesh that never changes can have its table
baked into flash instead (`tools/bake_edges.cpp` and
`scene.setOutlineEdges(mesh, edges)`, see `asset-pipeline.md`). If a mesh's
topology changes, or it is freed, call
`scene.renderer().invalidateOutline(&mesh)`. `TD_MATERIAL_WHITE` faces fill
white instead of black; decal faces are skipped. Only the shades 0.0 and 2.0
ever reach the target, so the frame is the same under every dither method.

Three per-object switches shape the lines:

- `obj.setInk(false)`: the object fills, and hides what is behind it, but
  never draws a line — an occluder, like the dark inside of a mouth.
- `obj.setFill(false)`: glass. The object fills nothing and hides nothing;
  its lines draw as the mode draws lines, and under `TD_OUTLINE` its back
  faces ink their crease and boundary edges too, so a bottle's far rim shows
  through its near wall. Every one of its lines is still depth-tested
  against the frame's fills: water inside the glass hides the far lines it
  stands in front of. Under `TD_SOLID` and `TD_CEL` only its edge ink draws,
  under `TD_WIREFRAME` its front edges, under `TD_SHADED` nothing. With both
  switches off an object draws nothing but still counts for the camera's
  fit.
- `obj.setInkGroup(n)` (1..255): objects in one group draw as one outline —
  parts sunk into each other, like cheeks on a head, show only the contour
  of their union. A frame with a group costs 1 B/pixel more.

### The depth test

By default the fills are painter-sorted and only the ink is depth-tested.
`scene.setOutlineDepthTest(true)` (or `setOutlineDepthTest` on a
`TD_Renderer` you compose with) depth-tests every fill pixel too, in
perspective-correct depth: a pixel is painted only where it is nearer than
what is already there or ties with it, and a tie goes to the later draw, so
a coplanar layer drawn later (squares on a board, marks on the squares)
still covers the one beneath. Within one pass a big ground face then does
not mis-sort against a small object standing on it.

Ink records its depth as well, and the depth persists across the
`renderPass()` calls of one renderer, so passes composed into one frame
occlude each other. `render()` clears it for its frame; when you compose
passes yourself, call that renderer's `clearOutlineDepth()` once per frame
before the first pass:

```cpp
TD_Renderer renderer;  // one renderer for every pass of the frame
renderer.setMode(TD_OUTLINE);
renderer.setOutlineDepthTest(true);

target.beginFrame();
renderer.clearOutlineDepth();
renderer.renderPass(board, board.camera, target);    // two TD_Scenes,
renderer.renderPass(pieces, pieces.camera, target);  // one frame
target.endFrame();
```

It uses the same 4 B/pixel depth buffer as the default and adds a compare
per filled pixel. Off by default; with it off every frame draws exactly as
without it.

## Lights

Every scene has a key light, `scene.light`, aimed from the upper left by
default (`setDirection(0.7f, 0.45f)`), and an ambient floor,
`scene.setAmbient(a)` (default 0.15). Add more directional lights, which
you own like objects:

```cpp
TD_Light fill;

void setup() {
  fill.setDirection(-2.4f, 0.3f);  // azimuth, elevation (radians): opposite the key
  fill.setIntensity(0.5f);
  scene.addLight(fill);
}
```

The key light always shades; added lights sum after it in the order they
were added, and the sum clamps at 1.0. `setIntensity(0)` silences a light;
`setHeadlight(scene.camera)` points one from the camera. Each added light
costs a little per lit corner every frame, so two or three is plenty; fills
and rims read well at 0.3–0.8. The direction convention is rule R7 in
`conventions.md`.
