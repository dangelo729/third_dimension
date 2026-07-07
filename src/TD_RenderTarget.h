// SPDX-License-Identifier: MIT
#ifndef TD_RENDERTARGET_H
#define TD_RENDERTARGET_H

#include <stdint.h>

#include "TD_Types.h"

// the seam. the renderer runs the whole geometry pipeline — transform,
// project, cull, painter sort, Gouraud shade, scanline setup — and hands the
// target shade values; the target decides pixels. crossed once per Gouraud
// SPAN (the hot path), not per pixel; cel banding may split a geometric span
// into several flat sub-spans, each still arriving through one plotShadeSpan
// call. per-pixel plotShade serves line drawing and is the default path for
// minimal custom targets (3 required overrides: width, height, plotShade).
//
// the contract:
//
// renderer guarantees to the target:
//   G1 (in-bounds)  every coordinate is inside [0,w) x [0,h) — targets may
//                   skip bounds checks in hot loops.
//   G2 (coverage)   every pixel covered by a front face is plotted, including
//                   fully dark ones (painter overwrite needs it).
//   G3 (endpoints)  BLACK faces / SOLID outlines / CEL black ink arrive as
//                   shade 0.0 exactly; WHITE faces / SOLID fills / WIREFRAME
//                   lines / CEL white ink as 2.0; LIT Gouraud shades are
//                   clamped to [ambient, 1.0], and CEL band levels arrive as
//                   exact slope-0 spans within that range.
//
// target guarantees to the renderer:
//   T1 (write, don't skip)  a plotted shade-0 pixel is a WRITE of the darkest
//                   value, never a no-op.
//   T2 (endpoints exact)    shade <= 0 produces the darkest output for the
//                   current material context, shade >= 1 the brightest.
//   T3 (containment)        a deferring target (error diffusion) only emits
//                   pixels plotted between beginFrame/endFrame.
//
// frame lifecycle: TD_Scene::render() is exactly one beginFrame..endFrame
// bracket. to composite several render passes into one frame (diffusion error
// must not double-run), bracket manually around TD_Renderer::renderPass().
class TD_RenderTarget {
 public:
  virtual ~TD_RenderTarget() {}

  virtual int16_t width() = 0;
  virtual int16_t height() = 0;

  // returning false aborts the render before any geometry is drawn.
  // built-in targets auto-clear here (opt-out via their setAutoClear).
  virtual bool beginFrame() { return true; }

  // once per face, after the painter sort, before that face's spans/lines.
  virtual void setMaterial(const TD_MaterialCtx &ctx) { (void)ctx; }

  // single pixel: lines, outlines, and the fallback span path.
  virtual void plotShade(int16_t x, int16_t y, float shade) = 0;

  // the hot path — one Gouraud span on row y covering columns xs..xe
  // inclusive. contract: the shade at column x is
  //     shade(x) = leftShade + (x + 0.5f - leftEdgeX) * shadeSlope
  // computed with exactly this expression (it is what the bit-exact golden
  // tests pin), and shadeSlope == 0.0f delivers exactly leftShade to every
  // pixel (flat-lit faces and saturated fills stay exact). the default
  // implementation loops plotShade; built-in targets override it to fuse
  // the loop and hoist per-span state.
  virtual void plotShadeSpan(int16_t y, int16_t xs, int16_t xe, float leftEdgeX,
                             float leftShade, float shadeSlope) {
    for (int x = xs; x <= xe; ++x)
      plotShade((int16_t)x, y, leftShade + (x + 0.5f - leftEdgeX) * shadeSlope);
  }

  // deferred-output hook: the diffusion dithers quantize the recorded frame
  // here. no-op for immediate targets.
  virtual void endFrame() {}
};

#endif  // TD_RENDERTARGET_H
