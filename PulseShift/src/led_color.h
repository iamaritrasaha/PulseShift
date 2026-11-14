// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>

// Shared LED color primitives used by the unified LED controller. There is no
// display abstraction here: everything below only describes WS2812B pixel data.
struct RgbColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  constexpr RgbColor() = default;
  constexpr RgbColor(uint8_t red, uint8_t green, uint8_t blue)
      : r(red), g(green), b(blue) {}

  constexpr bool operator==(const RgbColor& o) const {
    return r == o.r && g == o.g && b == o.b;
  }
  constexpr bool operator!=(const RgbColor& o) const {
    return !(*this == o);
  }
};

// Integer master-brightness scaling with rounding; factor 255 is identity.
inline constexpr RgbColor scaleColor(RgbColor c, uint8_t factor) {
  return {
    static_cast<uint8_t>((static_cast<uint16_t>(c.r) * factor + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c.g) * factor + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c.b) * factor + 127) / 255)
  };
}

// 256-entry CIE 1931 perceptual gamma correction table for WS2812B LEDs (gamma ~ 2.2).
// Eliminates linear PWM stepping and low-brightness popping in smooth animations.
static constexpr uint8_t GAMMA_TABLE[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,
    3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   7,   7,   7,   8,
    8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,  12,  13,  13,  14,  14,
   15,  15,  16,  16,  17,  17,  18,  19,  19,  20,  20,  21,  22,  22,  23,  24,
   24,  25,  26,  27,  27,  28,  29,  30,  30,  31,  32,  33,  34,  34,  35,  36,
   37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,
   53,  54,  55,  56,  58,  59,  60,  61,  62,  64,  65,  66,  67,  69,  70,  71,
   73,  74,  75,  77,  78,  80,  81,  83,  84,  86,  87,  89,  90,  92,  94,  95,
   97,  99, 100, 102, 104, 106, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125,
  127, 129, 131, 133, 135, 137, 140, 142, 144, 146, 149, 151, 153, 156, 158, 161,
  163, 166, 168, 171, 173, 176, 178, 181, 184, 186, 189, 192, 195, 197, 200, 203,
  206, 209, 212, 215, 218, 221, 224, 227, 230, 233, 236, 240, 243, 246, 250, 253,
  255
};

inline constexpr RgbColor scaleColorGamma(RgbColor c, uint8_t factor) {
  const uint8_t rLin = static_cast<uint8_t>((static_cast<uint16_t>(c.r) * factor + 127) / 255);
  const uint8_t gLin = static_cast<uint8_t>((static_cast<uint16_t>(c.g) * factor + 127) / 255);
  const uint8_t bLin = static_cast<uint8_t>((static_cast<uint16_t>(c.b) * factor + 127) / 255);
  return { GAMMA_TABLE[rLin], GAMMA_TABLE[gLin], GAMMA_TABLE[bLin] };
}
