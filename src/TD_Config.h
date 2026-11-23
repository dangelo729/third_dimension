// SPDX-License-Identifier: MIT
#ifndef TD_CONFIG_H
#define TD_CONFIG_H

// library-wide configuration: version, flash-placement macro, mesh-format
// handshake, and tuning constants. every other header includes this first.

#define THIRDDIMENSION_VERSION_MAJOR 0
#define THIRDDIMENSION_VERSION_MINOR 3
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

// line endpoints are clipped, in float space before any int conversion, to
// a band this many multiples of the target dimensions around the origin, so
// a line's pixel walk stays within a few target widths however far its
// projection runs. the clip is exact to float rounding of the line's
// length: an endpoint it leaves more than a pixel outside the band (a line
// longer than about 2^24 px) is held a pixel outside it, and a line whose
// span overflows a float draws nothing. fills need no band: their spans are
// clipped to the target itself.
#define TD_GUARD_BAND 4

// TD_CEL band-count ceiling. eight bands already exceed what the coarsest
// dither method can distinguish (BAYER2 carries 5 levels) and bound the
// per-span split loop.
#define TD_CEL_MAX_BANDS 8

// TD_OUTLINE default crease threshold, radians (60 degrees — the converter's
// default smoothing angle, so drawn feature lines match shading creases).
// changing it via setOutlineCrease invalidates the cached edge tables: the
// crease flags are baked at table build.
#define TD_OUTLINE_CREASE_DEFAULT 1.04719755f

// TD_OUTLINE ink depth tolerance, in screen pixels: an ink pixel is drawn
// where its edge's view depth is within this many pixels' worth of the
// face's steepest depth slope of the nearest fill stored at that pixel or
// one of its four neighbors. a bresenham line and the pixel-center fills
// around it disagree by up to about a pixel, so this keeps the faces beside
// a line from hiding it while a real occluder in front still does.
#define TD_OUTLINE_DEPTH_TOL_PX 1.5f

// TD_OUTLINE depth test tie tolerance, relative: under setOutlineDepthTest a
// fill pixel whose 1/z is within this fraction of the stored one counts as a
// tie, and the later draw wins it, so a coplanar layer drawn later covers
// the one beneath as the painter's order would.
#define TD_OUTLINE_DEPTH_TIE 1e-5f

#endif  // TD_CONFIG_H
