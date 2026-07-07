// SPDX-License-Identifier: MIT
#ifndef TD_CONFIG_H
#define TD_CONFIG_H

// library-wide configuration: version, flash-placement macro, mesh-format
// handshake, and tuning constants. every other header includes this first.

#define THIRDDIMENSION_VERSION_MAJOR 0
#define THIRDDIMENSION_VERSION_MINOR 1
#define THIRDDIMENSION_VERSION_PATCH 0

// the mesh header format this library reads. generated model headers
// (tools/obj2mesh.py) #error at compile time if this doesn't match the
// format they were emitted for, so a stale header is a readable compile
// error. any future layout change is a new format number with a new
// struct — never a mutation of TD_Mesh.
#define TD_MESH_FORMAT 1

// flash placement for generated mesh arrays. Teensy needs the PROGMEM
// section attribute to keep large tables out of RAM-backed flash copies;
// ESP32 / STM32 / RP2350 map const rodata into flash natively and the
// engine dereferences it directly (no pgm_read_* anywhere — AVR, which
// would need it, is rejected outright in ThirdDimension.h).
#if defined(TEENSYDUINO)
  #include <Arduino.h>
  #define TD_PROGMEM PROGMEM
#else
  #define TD_PROGMEM
#endif

// painter-sort depth pulled per decal layer, in model units (multiplied by
// the owning object's scale at sort time). large enough to dominate float
// noise in the mean-depth sums, far smaller than any real inter-face depth
// gap on a unit-radius mesh.
#define TD_DECAL_BIAS 0.002f

// projected coordinates are clamped to this many multiples of the target
// dimensions (in float space, before any int conversion) so near-skip-
// grazing geometry can't trigger float->int overflow or unbounded Bresenham
// walks off-screen.
#define TD_GUARD_BAND 4

// TD_CEL band-count ceiling. eight bands already exceed what the coarsest
// dither method can distinguish (BAYER2 carries 5 levels) and bound the
// per-span split loop.
#define TD_CEL_MAX_BANDS 8

#endif  // TD_CONFIG_H
