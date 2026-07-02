// SPDX-License-Identifier: MIT
// host test runner: owns the shared framebuffer infrastructure and executes
// the suites. build and run via the Makefile in this directory (`make run`).

#include <cstring>

#include "test_common.h"

uint8_t fb[H][W];
long oobCount = 0;
int failures = 0;
uint32_t lcg = 0x12345678u;

void tdTestPlot(void *user, int16_t x, int16_t y, uint8_t on) {
  (void)user;
  if (x < 0 || x >= W || y < 0 || y >= H) {
    ++oobCount;
    return;
  }
  fb[y][x] = on;
}

Stats stats() {
  Stats s;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (fb[y][x]) {
        ++s.count;
        if (x < s.minx) s.minx = x;
        if (x > s.maxx) s.maxx = x;
        if (y < s.miny) s.miny = y;
        if (y > s.maxy) s.maxy = y;
        s.cx += x;
        s.cy += y;
      }
  if (s.count) {
    s.cx /= s.count;
    s.cy /= s.count;
  }
  return s;
}

void clearFb() { std::memset(fb, 0, sizeof(fb)); }

static void dumpBorder() {
  char border[W + 3];
  border[0] = '+';
  for (int x = 0; x < W; ++x) border[x + 1] = '-';
  border[W + 1] = '+';
  border[W + 2] = 0;
  std::printf("%s\n", border);
}

void dump(const char *title) {
  std::printf("--- %s ---\n", title);
  dumpBorder();
  for (int y = 0; y < H; ++y) {
    char row[W + 1];
    for (int x = 0; x < W; ++x) row[x] = fb[y][x] ? '#' : ' ';
    row[W] = 0;
    std::printf("|%s|\n", row);
  }
  dumpBorder();
}

uint64_t fbHash() {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      h ^= fb[y][x];
      h *= 1099511628211ULL;
    }
  return h;
}

float frand(float lo, float hi) {
  lcg = lcg * 1664525u + 1013904223u;
  return lo + (hi - lo) * ((lcg >> 8) & 0xFFFFFF) / 16777216.0f;
}

int main() {
  runDitherTests();
  runRenderTests();
  runPrimitiveTests();

  if (failures == 0)
    std::printf("\nALL CHECKS PASSED\n");
  else
    std::printf("\n%d CHECK(S) FAILED\n", failures);
  return failures ? 1 : 0;
}
