# Coordinate & math conventions

Seven rules, fixed forever. Everything in the library — meshes, transforms,
culling, lighting, the golden test suite — assumes exactly these. Each rule
ends with what breaks if you assume otherwise, because that is how convention
bugs present.

## R1 — World and model space are right-handed, +Y up

+X right, +Y up, +Z into the scene — so the front of a front-facing model
faces −Z, toward the default camera. PicoCAD's OBJ export arrives in this
orientation directly; no axis flips happen in the import path or the engine.

*If you assume otherwise:* Z-up assets (default Blender scenes) come in
lying on their face — rotate them in the exporter (Y up, −Z forward), not
in code. A left-handed assumption mirrors the model and silently flips its
winding, which turns into rule R2's failure mode.

## R2 — Front faces wind counter-clockwise seen from outside

CCW-outward winding, which for a closed mesh means positive signed volume.
The OBJ converter (`tools/obj2mesh.py`) checks signed volume and warns when
it is negative, pointing to the escape hatch `renderer.setCullSign(-1.0f)`,
which flips the cull test at runtime for clockwise-wound meshes.

*If you assume otherwise:* back-face culling removes exactly the faces you
can see. The model renders inside-out, or seems to vanish at most
orientations. Fix the export where you can.

## R3 — View space: +X right, +Y up, +Z forward into the scene

The camera basis `{right, up, forward}` is orthonormal with determinant +1,
and `forward` points *into* the scene: visible geometry has positive view
z, and larger z is farther away.

*If you assume otherwise:* the common OpenGL habit (−Z forward) inverts
every depth comparison — the painter's back-to-front sort becomes
front-to-back, far objects draw over near ones, and hand-built view
matrices show you the scene behind the camera.

## R4 — Screen: x right, y down; exactly ONE y-flip exists

Screen coordinates follow the display convention (origin top-left, y grows
downward). The single flip between y-up view space and y-down screen space
is the projection line — `sy = cy − focal·y/z` — and nowhere else.
Consequence: front faces project with positive screen-space winding, and
the cull test keeps faces where `cullSign · cross > 0` with `cullSign = +1`
by default.

*If you assume otherwise:* adding a second flip in a custom target or
hand-rolled projection mirrors the image **and** reverses the projected
winding, so culling inverts too — it looks like R2 but is not fixable in
the exporter.

## R5 — Rotations are right-handed; object euler order is v' = Rx·Ry·Rz·v

Positive angles rotate counter-clockwise looking down the axis from its
positive end. `TD_Object::setRotation(rx, ry, rz)` applies **Z (roll)
first, then Y (yaw — the turntable spin), then X (pitch)**. This order is
an API commitment: it will not change, and animation code may rely on it.
All angles are radians (use Arduino's `radians()` for degrees).

*If you assume otherwise:* single-axis rotations look identical under any
order, so the bug hides until you combine axes — then compound poses land
somewhere unexpected and "pitch" starts rolling once yaw is nonzero.
Compose in the committed order, or build poses one axis at a time.

## R6 — Depth: view z increases away from the camera; near faces skip whole

The painter's algorithm sorts faces by mean view depth, descending (far
first). There is **no near-plane clipping**: any face with at least one
loop vertex closer than the camera's near-skip distance
(`setNearSkip`, default 0.1 world units) is skipped entirely for that
frame.

*If you assume otherwise:* code that treats small z as far inverts the
sort. And if you expect clipping, geometry grazing the camera looks like a
bug: whole faces pop in and out instead of slicing cleanly.
`renderer.lastNearSkipCount()` reports how many faces were dropped — if it
is nonzero, back the camera off or shrink the near-skip. See `limits.md`.

## R7 — TD_Light stores a world-space unit vector TOWARD the light

The vector points from surfaces to the light source; a corner is lit by a
light when `dot(n_world, L) > 0`. The convention applies to every light
identically: `scene.light` (the key light) and each light added via
`addLight` all store a world-space unit vector pointing toward the light.
Shade is the clamped sum over every light,

```
shade = clamp(ambient + Σᵢ (1 − ambient)·max(0, n·Lᵢ)·intensityᵢ, ≤ 1.0)
```

summed in a fixed order: the key light first, then added lights in add
order. `setDirection(azimuth, elevation)` builds

```
L = ( −sin(az)·cos(el),  sin(el),  −cos(az)·cos(el) )
```

— azimuth orbits world +Y with az 0 lighting from the −Z side (a headlight
for the default camera), elevation tilts the light upward. The light is a
world-space property: it does not move when the camera does.
`setHeadlight(camera)` sets `L = −camera.forward()` for a camera-attached
light at any pose.

*If you assume otherwise:* passing "the direction the light travels" (the
other common convention) into `setDirectionVector` lights the model from
the opposite side — the classic everything-is-backlit-and-dim bug. And
assuming light order is free: the sum is float, and its order is API.
