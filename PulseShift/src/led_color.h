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
