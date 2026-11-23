#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""obj2mesh.py — PicoCAD/Wavefront OBJ -> TD mesh format 1 C++ header.

One-time, host-side, per model:

    python3 tools/obj2mesh.py model.obj -o src/models/TD_Model_Model.h --name model

What it does:
  1. Parses `v` and `f` lines. Faces may be `v`, `v/vt`, `v//vn` or `v/vt/vn`;
     only the vertex index is used. PicoCAD's OBJ export writes dangling normal
     indices with no `vn` data — tolerated here. Indices are 1-based
     (negative = relative from end), converted to 0-based here.
  2. Fan-triangulates n-gons (PicoCAD faces are convex, so this is safe),
     keeping the original polygon loops so wireframe/solid modes can outline
     true face edges instead of fan diagonals.
  3. Centers the model on its bounding-box center and scales it to a unit
     bounding-sphere radius (the sketch picks the on-screen size via
     TD_Object::setScale); emits boundRadius = 1.0f accordingly.
  4. Precomputes per-corner (Gouraud) unit normals: each triangle corner gets
     the area-weighted average of the normals of every face meeting at that
     position whose orientation is within --crease degrees (default 60) of the
     corner's own face. Smooth surfaces (e.g. cylinder sides, 45 deg apart)
     shade per pixel with no facet popping; hard edges (90 deg caps) stay
     creased. Adjacency is by exact welded position, not vertex index, so
     exporters that duplicate vertices per face still smooth correctly.
  5. Sanity-checks winding via signed volume: for a closed CCW-outward mesh it
     is positive. Negative => the model would render inside-out; warn loudly
     (renderer.setCullSign(-1) is the escape hatch).
  6. Emits TD mesh format 1: a TD_MESH_FORMAT handshake, five
     `static const ... TD_PROGMEM` arrays, and a named
     `static const TD_Mesh <name>_mesh = { ... };` initializer (no macro, so
     multiple models coexist in one sketch).

Orientation: PicoCAD's OBJ export is already a conventional right-handed, Y-up,
CCW-outward mesh — no axis flips are applied. The library draws +Z into the
scene, so a right-handed model appears mirrored left for right
(extras/docs/conventions.md, R1). (The raw PicoCAD v1
.txt format needs an X/Y flip; this tool does not handle that format.)

Per-face PicoCAD colour (`c=`) does not survive the OBJ path, so no
per-face attribute table is emitted; the mesh's fattr is nullptr (all faces
TD_MATERIAL_LIT, no decals) and shading is purely the directional light.
"""
import argparse
import math
import os
import sys


def parse_obj(path):
    """Return (verts, faces): verts as [(x,y,z)...], faces as 0-based index lists."""
    verts = []
    faces = []
    with open(path, "r", encoding="utf-8", errors="replace") as fp:
        for lineno, raw in enumerate(fp, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            tok = line.split()
            if tok[0] == "v":
                if len(tok) < 4:
                    sys.exit(f"{path}:{lineno}: malformed vertex line: {raw.rstrip()}")
                verts.append(tuple(float(t) for t in tok[1:4]))
            elif tok[0] == "f":
                idx = []
                for t in tok[1:]:
                    s = t.split("/")[0]
                    if not s:
                        sys.exit(f"{path}:{lineno}: face token without vertex index: {t!r}")
                    i = int(s)
                    i = i - 1 if i > 0 else len(verts) + i  # 1-based / negative-relative
                    if not (0 <= i < len(verts)):
                        sys.exit(f"{path}:{lineno}: vertex index out of range: {t!r}")
                    idx.append(i)
                if len(idx) < 3:
                    sys.exit(f"{path}:{lineno}: face with fewer than 3 vertices")
                faces.append(idx)
    if not verts or not faces:
        sys.exit(f"{path}: no geometry found ({len(verts)} verts, {len(faces)} faces)")
    return verts, faces


def fan_triangulate(face):
    return [(face[0], face[k], face[k + 1]) for k in range(1, len(face) - 1)]


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_len(a):
    return math.sqrt(v_dot(a, a))


def newell_normal(loop, xverts):
    """Polygon normal via Newell's method: CCW-outward for right-handed CCW
    loops, robust for non-planar faces; magnitude = 2x polygon area."""
    nx = ny = nz = 0.0
    for k in range(len(loop)):
        a = xverts[loop[k]]
        b = xverts[loop[(k + 1) % len(loop)]]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return (nx, ny, nz)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("obj", help="input .obj file")
    ap.add_argument("-o", "--out", required=True, help="output .h path")
    ap.add_argument("--name", default="mesh",
                    help="C identifier prefix; the mesh symbol is <name>_mesh"
                         " (default: mesh)")
    ap.add_argument("--crease", type=float, default=60.0,
                    help="max angle (deg) between face normals that still "
                         "shades smooth (default: 60)")
    args = ap.parse_args()

    verts, faces = parse_obj(args.obj)

    if len(verts) > 65535:
        sys.exit(f"{args.obj}: {len(verts)} vertices exceeds uint16 index range")

    # --- center on bbox center, scale to unit bounding-sphere radius ---
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    center = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    radius = max(v_len(v_sub(v, center)) for v in verts)
    if radius <= 0.0:
        sys.exit(f"{args.obj}: degenerate model (zero radius)")
    scale = 1.0 / radius
    xverts = [tuple((v[i] - center[i]) * scale for i in range(3)) for v in verts]

    # --- per-face normals (Newell; fall back to fan-tri cross sums for the
    #     pathological cancelling non-planar case), then per-corner smoothed
    #     normals: faces meeting at a welded position contribute area-weighted
    #     iff within the crease angle of the corner's own face ---
    fnorm = []  # unnormalized (area-weighted)
    funit = []
    for f in faces:
        n = newell_normal(f, xverts)
        if v_len(n) < 1e-12:
            acc = (0.0, 0.0, 0.0)
            for t in fan_triangulate(f):
                c = v_cross(v_sub(xverts[t[1]], xverts[t[0]]),
                            v_sub(xverts[t[2]], xverts[t[0]]))
                acc = tuple(acc[i] + c[i] for i in range(3))
            n = acc
        ln = v_len(n)
        fnorm.append(n)
        funit.append(tuple(x / ln for x in n) if ln >= 1e-12 else (0.0, 0.0, 0.0))

    pos_faces = {}  # exact position -> face indices meeting there
    for fi, f in enumerate(faces):
        for vi in f:
            lst = pos_faces.setdefault(xverts[vi], [])
            if not lst or lst[-1] != fi:
                lst.append(fi)

    cos_crease = math.cos(math.radians(args.crease))
    corner_cache = {}

    def corner_normal(fi, vi):
        """Unit normal at face fi's corner on vertex vi (Gouraud, creased)."""
        key = (fi, xverts[vi])
        hit = corner_cache.get(key)
        if hit is not None:
            return hit
        nf = funit[fi]
        acc = (0.0, 0.0, 0.0)
        for gi in pos_faces[xverts[vi]]:
            if v_dot(nf, funit[gi]) >= cos_crease:
                acc = tuple(acc[i] + fnorm[gi][i] for i in range(3))
        ln = v_len(acc)
        n = tuple(x / ln for x in acc) if ln >= 1e-12 else nf
        corner_cache[key] = n
        return n

    # --- triangulate face by face, keeping per-face tri ranges and the original
    #     polygon loops (for true-edge outlines); drop zero-area triangles and
    #     faces left with no area at all ---
    out_tris = []      # fan triangles, face-major order
    cnorms = []        # 3 per kept triangle: per-corner unit normals
    face_meta = []     # per face: (tri_start, tri_count, loop_start, loop_count)
    loop_flat = []     # flattened polygon vertex loops
    dropped_tris = 0
    dropped_faces = 0
    for fi, f in enumerate(faces):
        tri_start = len(out_tris)
        for t in fan_triangulate(f):
            a, b, c = (xverts[t[0]], xverts[t[1]], xverts[t[2]])
            n = v_cross(v_sub(b, a), v_sub(c, a))
            ln = v_len(n)
            if ln < 1e-12:
                dropped_tris += 1
                continue
            out_tris.append(t)
            for vi in t:
                cnorms.append(corner_normal(fi, vi))
        tri_count = len(out_tris) - tri_start
        if tri_count == 0:
            dropped_faces += 1
            continue
        face_meta.append((tri_start, tri_count, len(loop_flat), len(f)))
        loop_flat.extend(f)

    if not out_tris:
        sys.exit(f"{args.obj}: all triangles degenerate")
    if len(out_tris) > 65535:
        sys.exit(f"{args.obj}: {len(out_tris)} triangles exceeds uint16 range")
    if 3 * len(out_tris) > 65535:
        sys.exit(f"{args.obj}: corner-normal table (3*{len(out_tris)} entries)"
                 " exceeds uint16 range")
    if len(loop_flat) > 65535:
        sys.exit(f"{args.obj}: face-loop table ({len(loop_flat)} entries) exceeds uint16 range")

    # --- winding sanity: signed volume (positive = CCW-outward for closed meshes) ---
    vol = sum(v_dot(xverts[t[0]], v_cross(xverts[t[1]], xverts[t[2]])) for t in out_tris) / 6.0

    name = args.name
    src = os.path.basename(args.obj)
    dropped_note = (f", {dropped_tris} degenerate tris / {dropped_faces} faces dropped"
                    if dropped_tris or dropped_faces else "")
    lines = []
    lines.append("// SPDX-License-Identifier: MIT")
    lines.append(f"// generated by tools/obj2mesh.py from {src} — do not edit.")
    lines.append(f"// {len(xverts)} verts, {len(out_tris)} tris, {len(face_meta)} faces"
                 f" (from {len(faces)} source faces{dropped_note}).")
    lines.append("// centered on bbox center, scaled to unit bounding-sphere radius.")
    lines.append(f"// signed volume {vol:+.4f} => winding {'CCW-outward (good)' if vol > 0 else 'SUSPECT'}.")
    lines.append("#pragma once")
    lines.append("#include <TD_Mesh.h>")
    lines.append("")
    lines.append("#if !defined(TD_MESH_FORMAT) || TD_MESH_FORMAT != 1")
    lines.append('#error "This model header is TD mesh format 1 — regenerate with a matching tools/obj2mesh.py"')
    lines.append("#endif")
    lines.append("")
    lines.append(f"static const float {name}_verts[{len(xverts)}][3] TD_PROGMEM = {{")
    for v in xverts:
        lines.append(f"  {{ {v[0]:.6f}f, {v[1]:.6f}f, {v[2]:.6f}f }},")
    lines.append("};")
    lines.append("")
    lines.append("// fan triangles, face-major order (0-based indices)")
    lines.append(f"static const uint16_t {name}_tris[{len(out_tris)}][3] TD_PROGMEM = {{")
    for t in out_tris:
        lines.append(f"  {{ {t[0]}, {t[1]}, {t[2]} }},")
    lines.append("};")
    lines.append("")
    lines.append("// per-corner unit normals (Gouraud), model space: 3 consecutive entries")
    lines.append(f"// per triangle in fan-corner order; crease angle {args.crease:g} deg")
    lines.append(f"static const float {name}_cnorms[{len(cnorms)}][3] TD_PROGMEM = {{")
    for n in cnorms:
        lines.append(f"  {{ {n[0]:.6f}f, {n[1]:.6f}f, {n[2]:.6f}f }},")
    lines.append("};")
    lines.append("")
    lines.append("// per source face: { triStart, triCount, loopStart, loopCount }")
    lines.append(f"static const uint16_t {name}_faces[{len(face_meta)}][4] TD_PROGMEM = {{")
    for fm in face_meta:
        lines.append(f"  {{ {fm[0]}, {fm[1]}, {fm[2]}, {fm[3]} }},")
    lines.append("};")
    lines.append("")
    lines.append("// flattened original polygon loops (true face edges, for outlines)")
    lines.append(f"static const uint16_t {name}_face_verts[{len(loop_flat)}] TD_PROGMEM = {{")
    for k in range(0, len(loop_flat), 16):
        lines.append("  " + ", ".join(str(i) for i in loop_flat[k:k + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("// TD mesh format 1 — all eleven fields explicit, in declaration order.")
    lines.append(f"static const TD_Mesh {name}_mesh = {{")
    lines.append(f"  {name}_verts,")
    lines.append(f"  {name}_tris,")
    lines.append(f"  {name}_cnorms,")
    lines.append(f"  {name}_faces,")
    lines.append(f"  {name}_face_verts,")
    lines.append("  nullptr,  // fattr: all faces TD_MATERIAL_LIT, no decals")
    lines.append(f"  {len(xverts)},  // nverts")
    lines.append(f"  {len(out_tris)},  // ntris")
    lines.append(f"  {len(face_meta)},  // nfaces")
    lines.append(f"  {len(loop_flat)},  // faceVertCount")
    lines.append("  1.0f,  // boundRadius (normalized to unit bounding sphere)")
    lines.append("};")
    lines.append("")

    with open(args.out, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))

    print(f"{src}: {len(verts)} verts, {len(faces)} faces -> {len(out_tris)} tris,"
          f" {len(face_meta)} kept faces{dropped_note}")
    print(f"  center {tuple(round(c, 4) for c in center)}  radius {radius:.4f}  scale {scale:.4f}")
    print(f"  signed volume {vol:+.4f}"
          + ("  [OK: CCW-outward winding]" if vol > 0 else
             "  [WARNING: non-positive — mesh may be open or wound inside-out;"
             " if it renders inside-out, call renderer.setCullSign(-1)]"))
    print(f"  wrote {args.out}")


if __name__ == "__main__":
    main()
