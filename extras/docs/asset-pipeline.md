# Asset pipeline: model to header

ThirdDimension renders meshes compiled into your sketch as C++ headers. The
pipeline is one dependency-free Python script:

```
PicoCAD / Blender / any OBJ tool
        │  export .obj
        ▼
tools/obj2mesh.py
        │  emits a mesh-format-1 C++ header
        ▼
#include "MyModel.h"  →  TD_Object obj(mymodel_mesh);
```

## Quick start

```sh
python3 tools/obj2mesh.py model.obj -o Model.h --name model
```

Copy `Model.h` next to your sketch and use it:

```cpp
#include <ThirdDimension_GFX.h>
#include "Model.h"

TD_Object model(model_mesh);   // <name>_mesh is the generated symbol
TD_Scene scene;

void setup() { scene.add(model); }
```

`--name model` sets the symbol prefix: the file emits
`static const TD_Mesh model_mesh`, so several models coexist in one sketch
with no macros to collide on. (The prefix defaults to `mesh`.)

## From PicoCAD

PicoCAD's OBJ export already matches the library's conventions — right-handed,
Y-up, counter-clockwise-outward winding — so no flips and no options are
needed. Export OBJ, not the raw PicoCAD `.txt` format; the converter reads OBJ
only.

```sh
python3 tools/obj2mesh.py mymodel.obj -o MyModel.h --name mymodel
```

PicoCAD writes dangling `v//vn` normal indices with no `vn` data. These are
tolerated: the converter reads only vertex indices and computes its own corner
normals (step 4).

## From Blender

Export with **+Y up, −Z forward** (the exporter defaults handle Z-up-to-Y-up),
triangulation OFF (the converter keeps original polygon loops for true-edge
outlines and whole-face depth sorting), and modifiers applied. Faces must be
convex — the converter fan-triangulates each polygon, which is only safe for
convex loops.

## What the converter does (the contract)

Stable across tool versions:

1. **Parses** `v`/`f` records — all face syntaxes (`f v`, `f v/t`, `f v//n`,
   `f v/t/n`), 1-based and negative indices. Only the vertex index is used.
2. **Fan-triangulates** n-gons while keeping the original loops; drops
   zero-area triangles and any face left empty by that dropping. Dropped
   counts appear in the summary output and the header banner.
3. **Normalizes**: centers on the bounding-box center, scales to a unit
   bounding-sphere radius, emits `boundRadius = 1.0f`. Not cosmetic — the decal
   depth bias and the camera's default near-skip both assume roughly unit-sized
   meshes. Set on-screen size with `TD_Object::setScale()`, not by scaling the
   asset.
4. **Computes per-corner normals** for Gouraud shading: area-weighted face
   normals (Newell's method) averaged per welded vertex position across faces
   within the crease angle of the corner's own face. Adjacency is by exact
   welded position, not vertex index. `--crease` defaults to 60° — edges
   sharper than that stay hard, flatter ones shade smooth.
5. **Checks winding** via signed volume; a closed CCW-outward mesh is positive.
   A non-positive result (negative = wound inside-out, zero = open or
   degenerate) warns loudly and points at the runtime escape hatch,
   `scene.renderer().setCullSign(-1.0f)`. Prefer fixing the export.
6. **Hard-errors** past any 16-bit limit — 65 535 vertices, triangles, or loop
   entries, and 3·ntris corner normals. No silent truncation.
7. **Emits** a self-contained header: a banner comment (source file, counts,
   signed volume), a `#pragma once`, `#include <TD_Mesh.h>`, the
   `TD_MESH_FORMAT` handshake (an `#if … #error` guard that turns a stale
   header into a readable compile error), five `static const … TD_PROGMEM`
   arrays, and the fully explicit `static const TD_Mesh <name>_mesh = { … };`.

`TD_PROGMEM` is the library's flash-placement macro — `PROGMEM` on Teensy,
empty on the flash-mapped MCUs (ESP32 / STM32 / RP2350, which map const rodata
into flash and dereference it directly) — so the mesh costs no RAM.

## Regenerating the shipped models

The two demo models in `src/models/` come from the OBJ sources in
`extras/assets/`; regenerate from the repository root with exactly:

```sh
python3 tools/obj2mesh.py extras/assets/barrel.obj \
    -o src/models/TD_Model_Barrel.h --name barrel
python3 tools/obj2mesh.py extras/assets/tall_guy.obj \
    -o src/models/TD_Model_TallGuy.h --name tall_guy
```

`TD_Model_Barrel.h` (symbol `barrel_mesh`) doubles as the host test fixture, so
regenerating it with a modified converter moves golden test pins — regenerate
deliberately.

## Multiple models, multiple objects

- **Many objects, one mesh** is cheap and supported:
  `TD_Object a(barrel_mesh), b(barrel_mesh);` share the flash arrays.
- **Include each model header from exactly ONE translation unit.** The
  generated arrays are `static const`, so including the header from N `.cpp`
  files duplicates it N times in flash. Include each model header only from the
  `.cpp` that declares its objects.
- Each header self-includes `<TD_Mesh.h>` (which pulls in the `TD_MESH_FORMAT`
  handshake), so it compiles regardless of include order or whether
  `ThirdDimension.h` is present.

## Exporting from the editor without the page

`tools/editor_bake.js` runs the editor's own export — the page's DOM-free
core script, lifted out of `editor.html` — so a header can be re-made from a
script: the input is a saved project `.json` or an exported `.h` (its
embedded project block), and the output is what the Export button writes.
Options change the project first, and the change is embedded in the new
header's project block like any edit:

```sh
JSC=/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc
$JSC tools/editor_bake.js -- tools/editor.html Model.h -o ModelSmall.h \
    --decimate 320,40,40 --name model_small    # per-part face targets, in part order
node tools/editor_bake.js tools/editor.html Model.h -o ModelSmall.h   # under node
```

`--decimate` takes one target per part (`-` keeps a part's own); `--name`
sets the symbol prefix. Under `jsc`, re-baking an exported header with no
options reproduces it byte for byte: both headers in `test/host` do. Under
node the output can differ where a part is decimated:
`TD_Model_ExportFixture.h` (no decimated part) re-bakes byte for byte, but
`TD_Model_DecimatedFixture.h` re-bakes to other vertices under node 26. Use
`jsc` to reproduce a header exactly.

## Baking a mesh's outline edge table

Under `TD_OUTLINE` the renderer builds an edge table per mesh at runtime: 3 B per
loop entry, plus 16 B/vertex + 8 B/loop entry + 12 B/face of scratch while it
builds. For a big mesh on a small board that scratch is the peak of the whole
heap. A mesh that never changes shape can have its table baked into flash
instead, by `tools/bake_edges.cpp` — a host C++ tool that runs the library's own
build, so the baked table is exactly the built one. From `tools/`:

```sh
clang++ -std=c++17 -O1 -ffp-contract=off -I../src -include Model.h \
    -DTD_BAKE_MESH=model_mesh bake_edges.cpp ../src/TD_Mesh.cpp \
    ../src/internal/td_edge_table.cpp -o bake_edges
./bake_edges model 0.8 > ModelEdges.h     # symbol prefix, crease in radians
```

The crease is the value the sketch passes to `setOutlineCrease` (read as a
float, so `0.8` here is `0.8f` there). Hand the table in at setup:

```cpp
#include "Model.h"
#include "ModelEdges.h"

scene.setOutlineCrease(0.8f);
scene.setOutlineEdges(model_mesh, model_edges);  // before prebuildOutline()
```

While the table's counts match the mesh and its crease matches the scene's, it
stands in for the built one; otherwise the table builds at runtime as usual, so
**re-bake whenever the mesh or the crease changes**. Include the edges header
from the same `.cpp` as the mesh header: the registration is keyed on the
mesh's address, and each translation unit gets its own copy of a `static const`
mesh.

## Checking a conversion

`td_validateMesh(model_mesh)` runs one-time structural validation — index
ranges, face-loop cursor continuity, decal host indices. Converter output
always passes, so the check exists for hand-built meshes and post-conversion
tinkering. The hot render path trusts the mesh and never re-validates.
