// SPDX-License-Identifier: MIT
#ifndef TD_TYPES_H
#define TD_TYPES_H

#include <stdint.h>

#include "TD_Config.h"

// shared enums and small POD types used across the library. plain C-style
// enums with uint8_t storage: keywords.txt can color bare constants, and
// the values index tables whose order is frozen (see TD_Dither).

// frame-level render style. set on the scene; target-agnostic. TD_SHADED is
// realized as dithering on 1-bit targets and as smooth color modulation on
// RGB565 targets — the mode names the intent, the target picks the pixels.
enum TD_RenderMode : uint8_t {
  TD_SHADED = 0,          // Gouraud-lit fill (dithered on mono targets)
  TD_SOLID = 1,           // saturated fill + black true-edge outlines
  TD_WIREFRAME = 2,       // front-facing polygon edges only
  TD_CEL = 3,             // lit fill quantized to flat bands + true-edge ink
  TD_RENDER_MODE_COUNT = 4,
};

// ink style for TD_CEL's true-edge outlines. BLACK draws at exact shade 0.0
// (matBlack on color targets), WHITE at exact shade 2.0 (matWhite) so lines
// read against dark 1-bit backgrounds, NONE disables the ink pass for pure
// banded fills. order is frozen, same doctrine as TD_Dither.
enum TD_CelInk : uint8_t {
  TD_CEL_INK_BLACK = 0,   // classic dark ink (the default; TD_SOLID's look)
  TD_CEL_INK_WHITE,       // light ink: rides the WHITE sentinel, pops on dark mono
  TD_CEL_INK_NONE,        // no outlines: pure screentone bands
  TD_CEL_INK_COUNT,
};

// 1-bit dither methods, selected on TD_MonoTarget. order is frozen: golden
// test hashes and the standard cycling idiom (index % TD_DITHER_COUNT) both
// index it. all but the last two are ordered/procedural — a pure function of
// (x, y, shade) — and share the invariants: shade<=0 never lights, shade>=1
// always lights, monotone in shade. FLOYD/ATKINSON are error diffusion: they
// run as a full-frame post-pass in TD_MonoTarget::endFrame (frame-coherent
// but not stable under motion — the classic crawl).
enum TD_Dither : uint8_t {
  TD_DITHER_BAYER8 = 0,   // classic 8x8 Bayer, 65 levels (the default)
  TD_DITHER_BAYER4,       // 4x4 Bayer, 17 levels: chunky retro crosshatch
  TD_DITHER_BAYER2,       // 2x2 Bayer, 5 levels: the stylistic extreme
  TD_DITHER_BLUE16,       // 16x16 blue noise (void-and-cluster): organic dots
  TD_DITHER_CLUSTER8,     // 8x8 clustered-dot halftone: newspaper dots
  TD_DITHER_IGN,          // interleaved gradient noise (Jimenez)
  TD_DITHER_SCANLINE,     // horizontal stripes, duty cycle tracks shade
  TD_DITHER_MONO,         // no dithering: hard on/off threshold at shade 0.5
  TD_DITHER_FLOYD,        // Floyd-Steinberg error diffusion (frame post-pass)
  TD_DITHER_ATKINSON,     // Atkinson diffusion: crushed classic-Mac look
  TD_DITHER_COUNT,
};

// per-face (or per-object) shading override. LIT is the normal Gouraud path.
// BLACK fills unconditionally dark and WHITE unconditionally lit under every
// dither method — both lean on the dither endpoint guarantees, so a BLACK
// face can never speckle and a WHITE face can never drop pixels. on color
// targets BLACK/WHITE render as the target's exact matBlack/matWhite palette
// colors, ignoring the light.
enum TD_Material : uint8_t {
  TD_MATERIAL_LIT = 0,
  TD_MATERIAL_BLACK = 1,
  TD_MATERIAL_WHITE = 2,
};

// why the last render() returned false (render() returns bool so sketches
// read forwards; the reason lives here).
enum TD_Status : uint8_t {
  TD_OK = 0,
  TD_ERR_NO_TARGET,       // target missing or reported non-positive dimensions
  TD_ERR_ALLOC,           // working-buffer allocation failed (nothing was drawn)
  TD_ERR_BAD_CAMERA,      // camera intent could not resolve (e.g. distance <= radius)
  TD_ERR_UNSUPPORTED,     // reserved for future targets; never returned in v1
                          //   (every mode x target cell is defined behavior)
};

#define TD_NO_HOST 0xFFFF

// optional per-face attributes (TD_Mesh::fattr). a face whose `host` is not
// TD_NO_HOST is a decal lying on another face: decals draw after every
// non-decal face, ordered among themselves by `layer` (0 = bottom). correct
// when the owning object is not occluded by another object.
struct TD_FaceAttr {
  uint8_t mat;     // TD_Material
  uint8_t layer;   // stacking order among coplanar decals
  uint16_t host;   // face index this decal sits on, or TD_NO_HOST
};

// per-face draw context handed to the render target after the painter sort,
// immediately before the face's spans/lines arrive. mono targets ignore it
// (materials reach them purely through shade encoding); the color target
// needs mat + baseColor to pick exact palette colors.
struct TD_MaterialCtx {
  uint8_t mat;         // TD_Material of the face about to draw
  uint8_t mode;        // TD_RenderMode (informational)
  uint16_t baseColor;  // owning object's RGB565 base color
  uint16_t objectIdx;  // scene object index (provenance/debug)
  uint16_t faceIdx;    // mesh-local face index (provenance/debug)
};

// pack 8-8-8 RGB into RGB565.
constexpr uint16_t td_color565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#endif  // TD_TYPES_H
