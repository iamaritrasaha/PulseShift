// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "shift_light.h"

ShiftLightEngine::ShiftLightEngine(const ShiftLightConfig& cfg)
    : cfg_(cfg) {
  if (cfg_.ledCount == 0) cfg_.ledCount = 1;
  if (cfg_.ledCount > MAX_LEDS) cfg_.ledCount = MAX_LEDS;
  if (!cfg_.startupAnimationEnabled) startupComplete_ = true;
  clearAllPixels();
}

void ShiftLightEngine::reset(uint32_t nowMs) {
  startupStartMs_ = nowMs;
  lastUpdateMs_ = 0;
  alertActive_ = false;
  alertStartMs_ = 0;
  startupComplete_ = !cfg_.startupAnimationEnabled;
  clearAllPixels();
  dirty_ = true;
}

void ShiftLightEngine::setMasterBrightness(uint8_t b) {
  cfg_.masterBrightness = b;
  dirty_ = true;
}

void ShiftLightEngine::setStyle(ShiftLightStyle s) {
  cfg_.style = s;
  dirty_ = true;
}

void ShiftLightEngine::setAlertMode(ShiftAlertMode m) {
  cfg_.alertMode = m;
  dirty_ = true;
}

void ShiftLightEngine::clearAllPixels() {
  for (size_t i = 0; i < MAX_LEDS; ++i) {
    logicalPixels_[i] = RgbColor{0, 0, 0};
    outputPixels_[i] = RgbColor{0, 0, 0};
  }
}

RgbColor ShiftLightEngine::profileColor(uint8_t index) const {
  const uint8_t n = cfg_.ledCount;
  if (cfg_.style == ShiftLightStyle::Classic) {
    if (n == 6) {
      if (index < 2) return RgbColor{0, 255, 0};      // Green
      if (index < 4) return RgbColor{255, 200, 0};    // Yellow
      if (index == 4) return RgbColor{255, 80, 0};     // Orange
      return RgbColor{255, 0, 0};                     // Red
    }
    const uint32_t frac = (uint32_t)index * 255 / (n > 1 ? n - 1 : 1);
    if (frac < 85) return RgbColor{0, 255, 0};
    if (frac < 170) return RgbColor{255, 200, 0};
    if (frac < 220) return RgbColor{255, 80, 0};
    return RgbColor{255, 0, 0};
  }

  if (cfg_.style == ShiftLightStyle::Racing) {
    if (n == 6) {
      if (index < 2) return RgbColor{0, 255, 60};     // Vivid Green
      if (index < 4) return RgbColor{255, 160, 0};    // Bright Amber
      if (index == 4) return RgbColor{255, 0, 90};     // Crimson
      return RgbColor{255, 0, 0};                     // Intense Red
    }
    const uint32_t frac = (uint32_t)index * 255 / (n > 1 ? n - 1 : 1);
    if (frac < 80) return RgbColor{0, 255, 60};
    if (frac < 150) return RgbColor{255, 160, 0};
    if (frac < 220) return RgbColor{255, 0, 90};
    return RgbColor{255, 0, 0};
  }

  if (cfg_.style == ShiftLightStyle::Minimal) {
    if (index == n - 1) return RgbColor{255, 0, 0};
    return RgbColor{220, 235, 255};                   // Crisp Ice White
  }

  if (cfg_.style == ShiftLightStyle::Custom) {
    if (index < MAX_LEDS &&
        (index < cfg_.customColorCount || cfg_.customColors[index].r != 0 ||
         cfg_.customColors[index].g != 0 ||
         cfg_.customColors[index].b != 0)) {
      return cfg_.customColors[index];
    }
    // Fallback to classic if unassigned
    return (index < n / 2) ? RgbColor{0, 255, 0} : RgbColor{255, 0, 0};
  }

  return RgbColor{0, 255, 0};
}

void ShiftLightEngine::renderStartup(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - startupStartMs_;
  const uint8_t n = cfg_.ledCount;

  if (elapsed >= 1200) {
    startupComplete_ = true;
    return;
  }

  if (elapsed < 400) {
    // Forward sweep: 0 -> n-1
    const uint32_t pos = elapsed * n / 400;
    const uint8_t activePos = (pos < n) ? static_cast<uint8_t>(pos) : (n - 1);
    for (uint8_t i = 0; i < n; ++i) {
      logicalPixels_[i] = (i == activePos) ? profileColor(i) : RgbColor{0, 0, 0};
    }
  } else if (elapsed < 800) {
    // Reverse sweep: n-1 -> 0
    const uint32_t dt = elapsed - 400;
    const uint32_t step = dt * n / 400;
    const uint8_t activePos = (step < n) ? static_cast<uint8_t>(n - 1 - step) : 0;
    for (uint8_t i = 0; i < n; ++i) {
      logicalPixels_[i] = (i == activePos) ? profileColor(i) : RgbColor{0, 0, 0};
    }
  } else if (elapsed < 1100) {
    // Confirmation pulse: triangle brightness wave
    const uint32_t dt = elapsed - 800;
    const uint8_t factor = (dt <= 150)
        ? static_cast<uint8_t>(dt * 255 / 150)
        : static_cast<uint8_t>((300 - dt) * 255 / 150);
    for (uint8_t i = 0; i < n; ++i) {
      logicalPixels_[i] = scaleColor(profileColor(i), factor);
    }
  } else {
    // Settle before live transition
    clearAllPixels();
  }
}

void ShiftLightEngine::renderActive(uint32_t nowMs, uint16_t rpm) {
  const uint8_t n = cfg_.ledCount;

  // Shift alert hysteresis
  if (!alertActive_) {
    if (rpm >= cfg_.rpmShift) {
      alertActive_ = true;
      alertStartMs_ = nowMs;
    }
  } else {
    const uint16_t clearRpm = (cfg_.rpmShift > cfg_.hysteresisRpm)
        ? (cfg_.rpmShift - cfg_.hysteresisRpm) : 0;
    if (rpm < clearRpm) {
      alertActive_ = false;
    }
  }

  if (alertActive_ && cfg_.alertMode != ShiftAlertMode::Off) {
    if (cfg_.alertMode == ShiftAlertMode::SolidRed) {
      for (uint8_t i = 0; i < n; ++i) {
        logicalPixels_[i] = RgbColor{255, 0, 0};
      }
    } else if (cfg_.alertMode == ShiftAlertMode::FlashRed) {
      const bool on = ((nowMs - alertStartMs_) % 100) < 50;
      for (uint8_t i = 0; i < n; ++i) {
        logicalPixels_[i] = on ? RgbColor{255, 0, 0} : RgbColor{0, 0, 0};
      }
    } else if (cfg_.alertMode == ShiftAlertMode::FlashWhite) {
      const bool on = ((nowMs - alertStartMs_) % 100) < 50;
      for (uint8_t i = 0; i < n; ++i) {
        logicalPixels_[i] = on ? RgbColor{255, 255, 255} : RgbColor{0, 0, 0};
      }
    }
    return;
  }

  // Progressive RPM indication
  if (rpm < cfg_.rpmStart) {
    if (cfg_.idleEffectEnabled && rpm <= cfg_.idleMaxRpm && rpm > 0) {
      // Gentle breathe on LED 0
      const uint32_t phase = nowMs % 2000;
      const uint8_t factor = (phase < 1000)
          ? static_cast<uint8_t>(phase * 35 / 1000 + 10)
          : static_cast<uint8_t>((2000 - phase) * 35 / 1000 + 10);
      logicalPixels_[0] = scaleColor(profileColor(0), factor);
      for (uint8_t i = 1; i < n; ++i) {
        logicalPixels_[i] = RgbColor{0, 0, 0};
      }
    } else {
      for (uint8_t i = 0; i < n; ++i) {
        logicalPixels_[i] = RgbColor{0, 0, 0};
      }
    }
    return;
  }

  const uint32_t span = (cfg_.rpmShift > cfg_.rpmStart) ? (cfg_.rpmShift - cfg_.rpmStart) : 1;
  const uint32_t clampedRpm = (rpm > cfg_.rpmShift) ? cfg_.rpmShift : rpm;
  const uint32_t progress = (uint32_t)(clampedRpm - cfg_.rpmStart) * (uint32_t)(n * 256) / span;

  for (uint8_t i = 0; i < n; ++i) {
    const uint32_t ledStart = (uint32_t)i * 256;
    const uint32_t ledEnd = (uint32_t)(i + 1) * 256;

    if (progress >= ledEnd) {
      logicalPixels_[i] = profileColor(i);
    } else if (progress > ledStart) {
      if (cfg_.smoothTransitions) {
        const uint8_t frac = static_cast<uint8_t>(progress - ledStart);
        logicalPixels_[i] = scaleColor(profileColor(i), frac);
      } else {
        logicalPixels_[i] = RgbColor{0, 0, 0};
      }
    } else {
      logicalPixels_[i] = RgbColor{0, 0, 0};
    }
  }
}

bool ShiftLightEngine::update(uint32_t nowMs, uint16_t rpm, ViewStatus status) {
  // Safety blanking must bypass the visual refresh interval.
  if (status == ViewStatus::Live && lastUpdateMs_ != 0 &&
      uint32_t(nowMs - lastUpdateMs_) < cfg_.animationIntervalMs) {
    return false;
  }
  // Resume valid data immediately after blanking instead of throttling recovery.
  lastUpdateMs_ = status == ViewStatus::Live ? nowMs : 0;

  if (status == ViewStatus::Fault || status == ViewStatus::Unconfigured) {
    startupComplete_ = true;
    alertActive_ = false;
    clearAllPixels();
  } else if (status != ViewStatus::Live) {
    alertActive_ = false;
    clearAllPixels();
  } else {
    if (!startupComplete_) {
      renderStartup(nowMs);
      if (startupComplete_) {
        renderActive(nowMs, rpm);
      }
    } else {
      renderActive(nowMs, rpm);
    }
  }

  const uint8_t n = cfg_.ledCount;
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t src = (cfg_.direction == LedDirection::Reversed) ? (n - 1 - i) : i;
    outputPixels_[i] = scaleColor(logicalPixels_[src], cfg_.masterBrightness);
  }

  dirty_ = (memcmp(outputPixels_, lastOutputPixels_, sizeof(RgbColor) * n) != 0);
  if (dirty_) {
    memcpy(lastOutputPixels_, outputPixels_, sizeof(RgbColor) * n);
  }
  return dirty_;
}

RgbColor ShiftLightEngine::getPixel(uint8_t index) const {
  if (index >= cfg_.ledCount) return RgbColor{0, 0, 0};
  return outputPixels_[index];
}
