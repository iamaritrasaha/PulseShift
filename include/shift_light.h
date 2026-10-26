// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "dashboard_model.h"

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

inline constexpr RgbColor scaleColor(RgbColor c, uint8_t factor) {
  return {
    static_cast<uint8_t>((static_cast<uint16_t>(c.r) * factor + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c.g) * factor + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c.b) * factor + 127) / 255)
  };
}

inline constexpr RgbColor blendColor(RgbColor c1, RgbColor c2, uint8_t ratio) {
  return {
    static_cast<uint8_t>((static_cast<uint16_t>(c1.r) * (255 - ratio) + static_cast<uint16_t>(c2.r) * ratio + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c1.g) * (255 - ratio) + static_cast<uint16_t>(c2.g) * ratio + 127) / 255),
    static_cast<uint8_t>((static_cast<uint16_t>(c1.b) * (255 - ratio) + static_cast<uint16_t>(c2.b) * ratio + 127) / 255)
  };
}

enum class ShiftLightStyle : uint8_t {
  Classic = 1,
  Racing = 2,
  Minimal = 3,
  Custom = 4
};

enum class ShiftAlertMode : uint8_t {
  Off = 0,
  SolidRed = 1,
  FlashRed = 2,
  FlashWhite = 3
};

enum class LedDirection : uint8_t {
  Normal = 1,
  Reversed = 2
};

struct ShiftLightConfig {
  uint8_t ledCount = 6;
  uint8_t masterBrightness = 40;
  ShiftLightStyle style = ShiftLightStyle::Classic;
  LedDirection direction = LedDirection::Normal;
  bool startupAnimationEnabled = true;
  bool smoothTransitions = true;
  ShiftAlertMode alertMode = ShiftAlertMode::FlashRed;
  uint16_t rpmStart = 1500;
  uint16_t rpmShift = 5800;
  uint16_t rpmMax = 6500;
  uint16_t hysteresisRpm = 150;
  uint16_t animationIntervalMs = 25;
  bool idleEffectEnabled = false;
  uint16_t idleMaxRpm = 1300;
  RgbColor customColors[32]{};
};

class ShiftLightEngine {
 public:
  static constexpr size_t MAX_LEDS = 32;

  explicit ShiftLightEngine(const ShiftLightConfig& cfg);

  void reset(uint32_t nowMs);
  bool update(uint32_t nowMs, uint16_t rpm, ViewStatus status);

  RgbColor getPixel(uint8_t index) const;
  uint8_t ledCount() const { return cfg_.ledCount; }
  bool isAlertActive() const { return alertActive_; }
  bool isStartupComplete() const { return startupComplete_; }

  void setMasterBrightness(uint8_t b);
  uint8_t masterBrightness() const { return cfg_.masterBrightness; }

  void setStyle(ShiftLightStyle s);
  ShiftLightStyle style() const { return cfg_.style; }

  void setAlertMode(ShiftAlertMode m);
  ShiftAlertMode alertMode() const { return cfg_.alertMode; }

  const ShiftLightConfig& config() const { return cfg_; }

 private:
  RgbColor profileColor(uint8_t index) const;
  void renderStartup(uint32_t nowMs);
  void renderActive(uint32_t nowMs, uint16_t rpm);
  void clearAllPixels();

  ShiftLightConfig cfg_;
  bool startupComplete_ = false;
  uint32_t startupStartMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  bool alertActive_ = false;
  uint32_t alertStartMs_ = 0;
  bool dirty_ = true;

  RgbColor logicalPixels_[MAX_LEDS]{};
  RgbColor outputPixels_[MAX_LEDS]{};
  RgbColor lastOutputPixels_[MAX_LEDS]{};
};
