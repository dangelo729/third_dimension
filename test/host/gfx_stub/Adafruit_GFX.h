// SPDX-License-Identifier: MIT
//
// a stand-in for the Adafruit GFX Library's Adafruit_GFX, for the host suite:
// only what ThirdDimension_GFX.h and test_gfx.cpp use, in the library's own
// shape (the raw and rotated sizes, the drawPixel / fillScreen / setRotation
// virtuals). the host build finds it with -Igfx_stub; nothing in src/ sees
// it.
#ifndef _ADAFRUIT_GFX_H
#define _ADAFRUIT_GFX_H

#include <stdint.h>

class Adafruit_GFX {
 public:
  Adafruit_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h), _width(w), _height(h), rotation(0) {}
  virtual ~Adafruit_GFX() {}

  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

  virtual void fillScreen(uint16_t color) {
    for (int16_t y = 0; y < _height; ++y)
      for (int16_t x = 0; x < _width; ++x) drawPixel(x, y, color);
  }

  virtual void setRotation(uint8_t r) {
    rotation = r & 3;
    _width = (rotation & 1) ? HEIGHT : WIDTH;
    _height = (rotation & 1) ? WIDTH : HEIGHT;
  }

  int16_t width(void) const { return _width; }
  int16_t height(void) const { return _height; }

 protected:
  int16_t WIDTH;
  int16_t HEIGHT;
  int16_t _width;
  int16_t _height;
  uint8_t rotation;
};

#endif  // _ADAFRUIT_GFX_H
