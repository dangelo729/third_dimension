// SPDX-License-Identifier: MIT
//
// ThirdDimension — a tiny scene-based 3D renderer for 32-bit
// microcontrollers, in the spirit of Adafruit GFX.
//
// this umbrella includes the dependency-free core only. for the optional
// Adafruit_GFX render targets, include <ThirdDimension_GFX.h> instead (it
// pulls in this header plus Adafruit_GFX.h). a sketch includes exactly one
// of the two.
//
// the core needs nothing beyond the C standard headers, which is what lets
// the full test suite run on a host PC.
#ifndef THIRDDIMENSION_H
#define THIRDDIMENSION_H

#if defined(__AVR__)
#error "ThirdDimension requires a 32-bit MCU with hardware float (Teensy 4.x, ESP32, STM32F4+, RP2350). AVR is not supported."
#endif

#include "TD_Config.h"
#include "TD_Types.h"
#include "TD_Math.h"
#include "TD_Mesh.h"
#include "TD_Object.h"
#include "TD_Camera.h"
#include "TD_Light.h"
#include "TD_RenderTarget.h"
#include "TD_Dither.h"
#include "TD_MonoTarget.h"
#include "TD_Color565Target.h"
#include "TD_Renderer.h"
#include "TD_Scene.h"
#include "TD_Primitives.h"

#endif  // THIRDDIMENSION_H
