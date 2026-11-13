// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_effects.h"
#include <string.h>
#include <math.h>

namespace {
// Per-frame hysteresis default; the persisted value lives in the config.
constexpr uint16_t DEFAULT_HYSTERESIS_RPM = 150;
constexpr uint16_t WARNING_FLASH_PERIOD_MS = 100;   // 10 Hz flash
constexpr uint16_t WARNING_STROBE_PERIOD_MS = 50;   // 20 Hz strobe
}  // namespace

LedEffectEngine::LedEffectEngine() {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    logicalPixels_[i] = RgbColor{0, 0, 0};
    outputPixels_[i] = RgbColor{0, 0, 0};
    lastOutputPixels_[i] = RgbColor{0, 0, 0};
    barLatched_[i] = false;
  }
}

void LedEffectEngine::reset(uint32_t nowMs) {
  startupStartMs_ = nowMs;
  startupComplete_ = false;
  warningLatched_ = false;
  warningStartMs_ = 0;
  for (uint8_t i = 0; i < LED_COUNT; ++i) barLatched_[i] = false;
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

void LedEffectEngine::setAllPixels(RgbColor color) {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    logicalPixels_[i] = color;
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
  else {
    // Unknown effect id: fall back to static.
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = cfg.ledColors[i];
    }
  }
}

// Six-stage threshold bar with per-LED hysteresis. LED i activates when the
// RPM rises past thresholds[i] and stays lit until RPM falls below
// thresholds[i] - hysteresis (no flicker on falling RPM).
void LedEffectEngine::renderRpmBar(uint32_t nowMs, const PulseShiftConfig& cfg, uint16_t rpm) {
  const uint16_t hyst = (cfg.rpmHysteresis > 0) ? cfg.rpmHysteresis : DEFAULT_HYSTERESIS_RPM;

  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const uint16_t onAt = cfg.rpmThresholds[i];
    const uint16_t offBelow = (onAt > hyst) ? static_cast<uint16_t>(onAt - hyst) : 0;
    if (!barLatched_[i]) {
      if (rpm >= onAt) barLatched_[i] = true;
    } else if (rpm < offBelow) {
      barLatched_[i] = false;
    }
    logicalPixels_[i] = barLatched_[i] ? cfg.ledColors[i] : RgbColor{0, 0, 0};
  }

  // Configurable shift warning with hysteresis, rendered above the bar.
  if (cfg.warningPatternId != 3 /* Off */) {
    const uint16_t warnOffBelow = (cfg.warningRpm > hyst)
        ? static_cast<uint16_t>(cfg.warningRpm - hyst) : 0;
    if (!warningLatched_) {
      if (rpm >= cfg.warningRpm) {
        warningLatched_ = true;
        warningStartMs_ = nowMs;
      }
    } else if (rpm < warnOffBelow) {
      warningLatched_ = false;
    }

    if (warningLatched_) {
      bool on = true;
      if (cfg.warningPatternId == 0 /* Flash */) {
        on = ((nowMs - warningStartMs_) / WARNING_FLASH_PERIOD_MS) % 2 == 0;
      } else if (cfg.warningPatternId == 1 /* Strobe */) {
        on = ((nowMs - warningStartMs_) / WARNING_STROBE_PERIOD_MS) % 2 == 0;
      }
      if (on) {
        setAllPixels(cfg.warningColor);
      } else {
        setAllPixels(RgbColor{0, 0, 0});
      }
    }
  }
}

bool LedEffectEngine::update(uint32_t nowMs, const PulseShiftConfig& cfg,
                             const LiveRpmSnapshot& rpm, const LedFrameInput& input) {
  // ---- Priority 1: hardware fault / safety state -----------------------
  // Cannot be masked by master power, decorative effects or commissioning.
  if (!input.canHealthy) {
    activeLayer_ = LedActiveLayer::Fault;
    const bool on = ((nowMs / 100) % 2) == 0;
    setAllPixels(on ? RgbColor{255, 0, 0} : RgbColor{0, 0, 0});
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 2: button feedback acknowledgment ----------------------
  // Cosmetic acknowledgment of a physical interaction. It wins over all
  // lighting layers but never over the fault indication above.
  if (input.feedbackActive) {
    activeLayer_ = LedActiveLayer::Feedback;
    setAllPixels(RgbColor{255, 255, 255});
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 3: RPM validity safeguard ------------------------------
  // Effect 4 is the protected, calibrated shift light. Effect 5 is a
  // separate, explicitly UNVERIFIED diagnostic preview of the live CAN signal.
  // Neither mode is allowed to display stale, unavailable or invalid RPM.
  // A provisional preview must still show non-RPM startup/commissioning
  // indications without a bike connected. Do not apply this exception to
  // the verified shift-light mode. Once startup finishes and no owner window
  // is open, stale/invalid RPM is strictly blanked as before.
  const bool previewIndicatorActive = cfg.effect == 5 &&
      (input.authWindowOpen || (cfg.isPowerOn && !startupComplete_));
  if (cfg.effect == 4 ||
      (cfg.effect == 5 && !input.testRpmActive && !previewIndicatorActive)) {
    const bool verifiedBar = cfg.effect == 4;
    const bool sampleQualified = rpm.available && rpm.fresh && rpm.valid &&
        (!verifiedBar || (cfg.calibrationVerified && rpm.calibrationVerified &&
                          rpm.calibrationMatchesSettings));
    if (!sampleQualified) {
      // Reset threshold latches so a recovered signal starts from fresh data.
      for (uint8_t i = 0; i < LED_COUNT; ++i) barLatched_[i] = false;
      warningLatched_ = false;
      activeLayer_ = LedActiveLayer::SafeguardBlank;
      setAllPixels(RgbColor{0, 0, 0});
      return finishFrame(nowMs, cfg);
    }
  }

  // ---- Priority 4: owner commissioning indication ----------------------
  if (input.authWindowOpen) {
    activeLayer_ = LedActiveLayer::Commissioning;
    const uint32_t phase = nowMs % 1000;
    const uint8_t factor = (phase < 500)
        ? static_cast<uint8_t>(phase * 255 / 500)
        : static_cast<uint8_t>((1000 - phase) * 255 / 500);
    const RgbColor cyan = scaleColor(RgbColor{0, 255, 255}, factor);
    setAllPixels(cyan);
    return finishFrame(nowMs, cfg);
  }

  // ---- Master power switch ---------------------------------------------
  if (!cfg.isPowerOn) {
    activeLayer_ = LedActiveLayer::Off;
    setAllPixels(RgbColor{0, 0, 0});
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 5: startup indication ----------------------------------
  if (!startupComplete_) {
    renderStartup(nowMs, cfg);
    if (!startupComplete_) {
      activeLayer_ = LedActiveLayer::Startup;
      return finishFrame(nowMs, cfg);
    }
  }

  // Explicit synthetic RPM bench test works even when no CAN data is present.
  // The separate verified mode (effect 4) preserves its original priority.
  if (input.testRpmActive && cfg.effect == 5) {
    activeLayer_ = LedActiveLayer::BenchTest;
    renderRpmBar(nowMs, cfg, input.testRpm);
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 6: verified RPM bar / provisional diagnostic preview ---
  if (cfg.effect == 4 || cfg.effect == 5) {
    activeLayer_ = (cfg.effect == 4) ? LedActiveLayer::RpmBar
                                     : LedActiveLayer::ProvisionalRpm;
    renderRpmBar(nowMs, cfg, rpm.rpm);
    return finishFrame(nowMs, cfg);
  }
  if (input.testRpmActive) {
    activeLayer_ = LedActiveLayer::BenchTest;
    renderRpmBar(nowMs, cfg, input.testRpm);
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 7: manual decorative lighting --------------------------
  activeLayer_ = LedActiveLayer::Decorative;
  renderEffect(nowMs, cfg);
  return finishFrame(nowMs, cfg);
}

bool LedEffectEngine::finishFrame(uint32_t nowMs, const PulseShiftConfig& cfg) {
  // Apply LED ordering (direction) & master brightness scaling, then detect
  // the change against the PREVIOUS physical frame. The previous-frame
  // buffer is refreshed every call so unchanged frames return false and no
  // redundant WS2812B write is issued; lit-to-black transitions return true
  // and therefore always produce a physical leds_.show() call.
  bool changed = false;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const uint8_t srcIdx = (cfg.ledDirection == 1) ? (LED_COUNT - 1 - i) : i;
    outputPixels_[i] = scaleColor(logicalPixels_[srcIdx], cfg.brightness);
    if (outputPixels_[i] != lastOutputPixels_[i]) changed = true;
  }
  if (changed) {
    memcpy(lastOutputPixels_, outputPixels_, sizeof(outputPixels_));
  }
  (void)nowMs;
  return changed;
}
