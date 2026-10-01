// SPDX-License-Identifier: MIT
// editor_bake.js — tools/editor.html's export, headless. it lifts the
// editor's own core script (the <script id="td-core"> block: pure logic, no
// DOM) out of editor.html and runs exactly what the Export button runs —
// parseProjectText -> bake -> emitHeader — so the header is the one the
// editor would write. the input is a saved project .json or an exported .h
// (its embedded project block). runs under the jsc shell every mac ships,
// or under node:
//
//   /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc \
//       tools/editor_bake.js -- tools/editor.html model.h -o Model.h [options]
//   node tools/editor_bake.js tools/editor.html model.h -o Model.h [options]
//
// options change the project before the bake, and the change is embedded in
// the header's project block like any other edit:
//   --decimate T1,T2,...  each part's decimate target (faces), in part order;
//                         "-" keeps a part's own setting
//   --name NAME           the project name: the header's symbol prefix
//                         (NAME_mesh)
// the header is written only after a clean bake; any failure throws, and
// the shell exits nonzero.

"use strict";

const host = typeof process !== "undefined" && process.versions && process.versions.node
  ? {
      args: process.argv.slice(2),
      read: (p) => require("fs").readFileSync(p, "utf8"),
      write: (p, text) => require("fs").writeFileSync(p, text),
      log: (s) => console.error(s),
    }
  : {
      args: Array.from(arguments),
      read: (p) => readFile(p),
      write: (p, text) => writeFile(p, text),
      log: (s) => printErr(s),
    };

function usage(msg) {
  throw new Error(msg + "\nusage: editor_bake.js <editor.html> <project.json|model.h> -o <out.h>" +
                  " [--decimate T1,T2,...] [--name NAME]");
}

// the arguments
const positional = [];
let outPath = null, decimate = null, name = null;
for (let i = 0; i < host.args.length; ++i) {
  const a = host.args[i];
  if (a === "-o") outPath = host.args[++i];
  else if (a === "--decimate") decimate = host.args[++i];
  else if (a === "--name") name = host.args[++i];
  else if (a.startsWith("-")) usage("unknown option " + a);
  else positional.push(a);
}
if (positional.length !== 2) usage("expected the editor and one project");
if (!outPath) usage("expected -o <out.h>");
const [editorPath, projectPath] = positional;

// the editor's core, lifted out of its page
const html = host.read(editorPath);
const open = html.indexOf('<script id="td-core">');
const close = html.indexOf("</script>", open);
if (open < 0 || close < 0) throw new Error(editorPath + ": no <script id=\"td-core\"> block");
const coreText = html.slice(html.indexOf(">", open) + 1, close);
const C = (0, eval)(coreText + "\n;TDCore;");

// the project, with the overrides applied
const project = C.parseProjectText(host.read(projectPath));
if (decimate !== null) {
  const targets = decimate.split(",");
  if (targets.length !== project.parts.length)
    usage(`--decimate has ${targets.length} targets for ${project.parts.length} parts`);
  targets.forEach((t, k) => {
    if (t === "-") return;
    const n = Number(t);
    if (!Number.isInteger(n) || n < 4) usage(`--decimate target "${t}" is not a whole number of faces >= 4`);
    project.parts[k].decimate = { target: n };
  });
}
if (name !== null) project.name = name;
const errs = C.validateProject(project);
if (errs.length) throw new Error("invalid project:\n  " + errs.join("\n  "));

// the export button
const baked = C.bake(project);
if (!baked.ok) throw new Error(baked.error);
for (const [a, b] of baked.overlaps)
  host.log(`note: parts ${a.id} (${a.type}) and ${b.id} (${b.type}) interpenetrate`);
const hdr = C.emitHeader(project, baked);
host.write(outPath, hdr.text);
const m = baked.mesh;
host.log(`${outPath}: ${m.nverts} verts, ${m.ntris} tris, ${m.nfaces} faces` +
         ` (parts: ${project.parts.map((p) => C.decimateTarget(p) || "full").join(", ")})`);
