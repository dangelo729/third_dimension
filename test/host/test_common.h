// SPDX-License-Identifier: MIT
#ifndef TD_TEST_COMMON_H
#define TD_TEST_COMMON_H

// shared infrastructure for the host test suites. every render lands in one
// 128x32 byte framebuffer through a callback with the exact shape of
// TD_MonoTarget::PlotFn / TD_DiffusePlotFn; a plot outside the framebuffer
// increments a trap counter instead of writing (renderer contract G1 says
// that counter stays zero forever). definitions live in test_main.cpp.

#include <stdint.h>

#include <cstdio>

// framebuffer / mono-target dimensions used by every suite.
static const int W = 128;
static const int H = 32;

// fb[y][x] holds the last plotted value (0 dark, 1 lit). tests may preload
// other byte values (e.g. 2) as a "never plotted" tracer: a dark plot is a
// write of 0 (target contract T1), never a skip, so tracer bytes survive
// only where nothing was plotted.
extern uint8_t fb[H][W];

// incremented instead of writing when a plot lands outside [0,W) x [0,H).
extern long oobCount;

// total CHECK failures across all suites; the process exit code keys off it.
extern int failures;

// matches TD_MonoTarget::PlotFn and TD_DiffusePlotFn; `user` is unused.
void tdTestPlot(void *user, int16_t x, int16_t y, uint8_t on);

// non-fatal assertion: records the failure and keeps going so a single run
// reports every broken invariant.
#define CHECK(cond, ...)                                \
  do {                                                  \
    if (!(cond)) {                                      \
      ++failures;                                       \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
      std::printf(__VA_ARGS__);                         \
      std::printf("\n");                                \
    }                                                   \
  } while (0)

// lit-pixel census over fb: count, bounding box, centroid. any nonzero byte
// counts as lit, so tracer values must be cleared before using this.
struct Stats {
  long count = 0;
  int minx = W, maxx = -1, miny = H, maxy = -1;
  double cx = 0, cy = 0;
};
Stats stats();

// memset the framebuffer to the all-dark state every render starts from.
void clearFb();

// ASCII frame dump for eyeball verification.
void dump(const char *title);

// FNV-1a over the raw framebuffer bytes, row-major — the full-frame golden
// hash the render suite pins.
uint64_t fbHash();

// deterministic LCG so fuzz runs are reproducible. the state is exposed so
// a suite can save/restore the stream around its own draws and leave the
// later suites' fuzz inputs unchanged.
extern uint32_t lcg;
float frand(float lo, float hi);

// suite entry points, run in this order by test_main.cpp.
void runDitherTests();
void runRenderTests();
void runCelTests();
void runPrimitiveTests();

#endif  // TD_TEST_COMMON_H
