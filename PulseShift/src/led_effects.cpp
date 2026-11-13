// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_effects.h"
#include <string.h>
#include <math.h>

LedEffectEngine::LedEffectEngine() {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    logicalPixels_[i] = RgbColor{0, 0, 0};
    outputPixels_[i] = RgbColor{0, 0, 0};
    lastOutputPixels_[i] = RgbColor{0, 0, 0};
  }
}

void LedEffectEngine::reset(uint32_t nowMs) {
  startupStartMs_ = nowMs;
  lastUpdateMs_ = 0;
  startupComplete_ = false;
  dirty_ = true;
}

RgbColor LedEffectEngine::hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0) return RgbColor{v, v, v};

  const uint8_t region = h / 43; // 0..5
  const uint8_t remainder = (h - (region * 43)) * 6; // 0..255

  const uint8_t p = (static_cast<uint16_t>(v) * (255 - s)) >> 8;
  const uint8_t q = (static_cast<uint16_t>(v) * (255 - ((static_cast<uint16_t>(s) * remainder) >> 8))) >> 8;
  const uint8_t t = (static_cast<uint16_t>(v) * (255 - ((static_cast<uint16_t>(s) * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0:  return RgbColor{v, t, p};
    case 1:  return RgbColor{q, v, p};
    case 2:  return RgbColor{p, v, t};
    case 3:  return RgbColor{p, q, v};
    case 4:  return RgbColor{t, p, v};
    default: return RgbColor{v, p, q};
  }
}

void LedEffectEngine::renderStartup(uint32_t nowMs, const PulseShiftConfig& cfg) {
  const uint32_t elapsed = nowMs - startupStartMs_;
  const uint32_t dur = (cfg.startupDurationMs > 0) ? cfg.startupDurationMs : 2000;

  if (elapsed >= dur || cfg.startupAnimation == 0) {
    startupComplete_ = true;
    return;
  }

  // Animation 1: Sweep (Sequential ignition wipe across LEDs 0..5)
  if (cfg.startupAnimation == 1) {
    const uint32_t half = dur / 2;
    if (elapsed < half) {
      // Forward wipe
      const uint32_t activeIdx = elapsed * LED_COUNT / half;
      for (uint8_t i = 0; i < LED_COUNT; ++i) {
        logicalPixels_[i] = (i <= activeIdx) ? cfg.ledColors[i] : RgbColor{0, 0, 0};
      }
    } else {
      // Fade into active color
      const uint32_t dt = elapsed - half;
      const uint8_t factor = static_cast<uint8_t>(255 - (dt * 155 / half));
      for (uint8_t i = 0; i < LED_COUNT; ++i) {
        logicalPixels_[i] = scaleColor(cfg.ledColors[i], factor);
      }
    }
  }
  // Animation 2: Wave (Dual-crested spatial wave)
  else if (cfg.startupAnimation == 2) {
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      const float phase = (elapsed * 6.28318f / dur) + (i * 1.047f);
      const float sinVal = (sinf(phase) + 1.0f) * 0.5f; // 0.0 .. 1.0
      const uint8_t brightness = static_cast<uint8_t>(sinVal * 255.0f);
      logicalPixels_[i] = scaleColor(cfg.ledColors[i], brightness);
    }
  }
  // Animation 3: Pulse (Heartbeat dual pulse)
  else if (cfg.startupAnimation == 3) {
    const uint32_t phaseMs = elapsed % (dur / 2);
    const uint32_t pulseDur = dur / 4;
    uint8_t factor = 0;
    if (phaseMs < pulseDur) {
      factor = (phaseMs < pulseDur / 2)
          ? static_cast<uint8_t>(phaseMs * 255 / (pulseDur / 2))
          : static_cast<uint8_t>((pulseDur - phaseMs) * 255 / (pulseDur / 2));
    }
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = scaleColor(cfg.ledColors[i], factor);
    }
  }
}

void LedEffectEngine::renderEffect(uint32_t nowMs, const PulseShiftConfig& cfg) {
  // Effect 0: Static Color
  if (cfg.effect == 0) {
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = cfg.ledColors[i];
    }
  }
  // Effect 1: Breathing (Soft sine-wave rise and fall)
  else if (cfg.effect == 1) {
    // Speed: 0..100 maps to breathing period 4000ms .. 800ms
    const uint32_t period = 4000 - (cfg.effectSpeed * 32);
    const float phase = (nowMs % period) * 6.28318f / period;
    const float sinVal = (sinf(phase) + 1.0f) * 0.45f + 0.1f; // 0.1 .. 1.0
    const uint8_t factor = static_cast<uint8_t>(sinVal * 255.0f);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = scaleColor(cfg.ledColors[i], factor);
    }
  }
  // Effect 2: Rainbow (Smooth full-spectrum HSV color wave)
  else if (cfg.effect == 2) {
    // Speed: 0..100 maps to hue rate
    const uint32_t speedFactor = (cfg.effectSpeed > 0) ? cfg.effectSpeed : 10;
    const uint8_t baseHue = static_cast<uint8_t>((nowMs * speedFactor / 40) % 256);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      const uint8_t hue = (baseHue + (i * 256 / LED_COUNT)) % 256;
      logicalPixels_[i] = hsvToRgb(hue, 255, 255);
    }
  }
  // Effect 3: Color Chase (High-velocity runner traversing LEDs)
  else if (cfg.effect == 3) {
    const uint32_t stepMs = 350 - (cfg.effectSpeed * 3); // 350ms down to 50ms per step
    const uint32_t head = (nowMs / stepMs) % LED_COUNT;
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      if (i == head) {
        logicalPixels_[i] = cfg.ledColors[i];
      } else {
        const uint8_t prev = (head > 0) ? (head - 1) : (LED_COUNT - 1);
        logicalPixels_[i] = (i == prev) ? scaleColor(cfg.ledColors[i], 80) : RgbColor{0, 0, 0};
      }
    }
  }
  // Effect 4: RPM Bar fallback (if calibration not verified, fallback to static)
  else {
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = cfg.ledColors[i];
    }
  }
}

bool LedEffectEngine::update(uint32_t nowMs, const PulseShiftConfig& cfg, bool authWindowActive) {
  // If authorization window is active, pulse cyan across all LEDs as physical owner feedback
  if (authWindowActive) {
    const uint32_t phase = nowMs % 1000;
    const uint8_t factor = (phase < 500)
        ? static_cast<uint8_t>(phase * 255 / 500)
        : static_cast<uint8_t>((1000 - phase) * 255 / 500);
    const RgbColor cyan = scaleColor(RgbColor{0, 255, 255}, factor);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      outputPixels_[i] = scaleColor(cyan, cfg.brightness);
    }
    dirty_ = (memcmp(outputPixels_, lastOutputPixels_, sizeof(outputPixels_)) != 0);
    if (dirty_) memcpy(lastOutputPixels_, outputPixels_, sizeof(outputPixels_));
    return dirty_;
  }

  // Master Power Switch
  if (!cfg.isPowerOn) {
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      outputPixels_[i] = RgbColor{0, 0, 0};
    }
    dirty_ = (memcmp(outputPixels_, lastOutputPixels_, sizeof(outputPixels_)) != 0);
    if (dirty_) memcpy(lastOutputPixels_, outputPixels_, sizeof(outputPixels_));
    return dirty_;
  }

  // Handle Startup Animation
  if (!startupComplete_) {
    renderStartup(nowMs, cfg);
  } else {
    renderEffect(nowMs, cfg);
  }

  // Apply Direction & Master Brightness Scaling
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const uint8_t srcIdx = (cfg.ledDirection == 1) ? (LED_COUNT - 1 - i) : i;
    outputPixels_[i] = scaleColor(logicalPixels_[srcIdx], cfg.brightness);
  }

  dirty_ = (memcmp(outputPixels_, lastOutputPixels_, sizeof(outputPixels_)) != 0);
  if (dirty_) {
    memcpy(lastOutputPixels_, outputPixels_, sizeof(outputPixels_));
  }
  return dirty_;
}
