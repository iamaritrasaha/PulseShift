// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "shift_light.h"
#include "config_manager.h"

class LedEffectEngine {
 public:
  static constexpr uint8_t LED_COUNT = 6;

  LedEffectEngine();

  void reset(uint32_t nowMs);
  bool update(uint32_t nowMs, const PulseShiftConfig& cfg, bool authWindowActive);

  RgbColor getPixel(uint8_t index) const {
    if (index >= LED_COUNT) return RgbColor{0, 0, 0};
    return outputPixels_[index];
  }

  bool isStartupComplete() const { return startupComplete_; }

 private:
  void renderStartup(uint32_t nowMs, const PulseShiftConfig& cfg);
  void renderEffect(uint32_t nowMs, const PulseShiftConfig& cfg);

  // Fast integer HSV to RGB converter (h: 0..255, s: 0..255, v: 0..255)
  static RgbColor hsvToRgb(uint8_t h, uint8_t s, uint8_t v);

  bool startupComplete_ = false;
  uint32_t startupStartMs_ = 0;
  uint32_t lastUpdateMs_ = 0;

  RgbColor logicalPixels_[LED_COUNT]{};
  RgbColor outputPixels_[LED_COUNT]{};
  RgbColor lastOutputPixels_[LED_COUNT]{};
  bool dirty_ = true;
};
