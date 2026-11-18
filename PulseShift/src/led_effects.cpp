// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_effects.h"
#include <string.h>
#include <math.h>

namespace {
constexpr uint16_t WARNING_FLASH_PERIOD_MS = 100;   // 10 Hz flash
constexpr uint16_t WARNING_STROBE_PERIOD_MS = 50;   // 20 Hz strobe
}  // namespace

LedEffectEngine::LedEffectEngine() {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    logicalPixels_[i] = RgbColor{0, 0, 0};
    outputPixels_[i] = RgbColor{0, 0, 0};
    lastCalculatedOutputPixels_[i] = RgbColor{0, 0, 0};
    barLatched_[i] = false;
  }
}

void LedEffectEngine::reset(uint32_t nowMs) {
  startupStartMs_ = nowMs;
  startupComplete_ = false;
  barFromBenchTest_ = false;
  resetRpmLatches();
}

void LedEffectEngine::resetRpmLatches() {
  warningLatched_ = false;
  for (auto& latched : barLatched_) latched = false;
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
  const uint32_t dur = cfg.startupDurationMs & 0x7FFFu;
  const bool looping = (cfg.startupDurationMs & 0x8000u) != 0;
  const uint32_t totalElapsed = nowMs - startupStartMs_;
  const uint32_t elapsed = looping ? totalElapsed % dur : totalElapsed;

  if ((!looping && totalElapsed >= dur) || cfg.startupAnimation == 0) {
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
  // Animation 4: Center-Out (Staging Tree - expanding from center LEDs 2,3 outward to 0,5)
  else if (cfg.startupAnimation == 4) {
    const uint32_t stepDur = (dur > 3) ? (dur / 3) : 1;
    const uint32_t step = (elapsed >= dur) ? 2 : (elapsed / stepDur);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      bool active = false;
      if (step == 0 && (i == 2 || i == 3)) active = true;
      else if (step == 1 && (i >= 1 && i <= 4)) active = true;
      else if (step >= 2) active = true;
      logicalPixels_[i] = active ? cfg.ledColors[i] : RgbColor{0, 0, 0};
    }
  }
  // Animation 5: Strobe Rush (F1 Grid Start - multi-burst sequence transitioning to full solid)
  else if (cfg.startupAnimation == 5) {
    const uint32_t flashPeriod = (dur > 6) ? (dur / 6) : 1;
    const bool flashOn = (elapsed >= (dur * 2) / 3) || ((elapsed / flashPeriod) % 2 == 0);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = flashOn ? cfg.ledColors[i] : RgbColor{0, 0, 0};
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
  // Effect 1: Breathing (Perceptually smooth sinusoidal pulse)
  else if (cfg.effect == 1) {
    const uint32_t period = (cfg.effectSpeed > 0) ? (4000 - (cfg.effectSpeed * 32)) : 4000;
    const float phase = (period > 0) ? ((nowMs % period) * 6.2831853f / period) : 0.0f;
    const float sinVal = (sinf(phase) + 1.0f) * 0.45f + 0.1f; // 0.1 .. 1.0
    const uint8_t factor = static_cast<uint8_t>(sinVal * 255.0f);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      logicalPixels_[i] = scaleColorGamma(cfg.ledColors[i], factor);
    }
  }
  // Effect 2: Rainbow (Smooth full-spectrum HSV color wave)
  else if (cfg.effect == 2) {
    // Speed: 0..100 maps to hue rate
    const uint32_t speedFactor = (cfg.effectSpeed > 0) ? cfg.effectSpeed : 10;
    const uint8_t baseHue = static_cast<uint8_t>((nowMs % (40 * 256)) * speedFactor / 40);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      const uint8_t hue = (baseHue + (i * 256 / LED_COUNT)) % 256;
      logicalPixels_[i] = hsvToRgb(hue, 255, 255);
    }
  }
  // Effect 3: Color Chase (Smooth runner traversing LEDs with anti-aliasing)
  else if (cfg.effect == 3) {
    const uint32_t stepMs = (cfg.effectSpeed <= 100) ? (350 - (cfg.effectSpeed * 3)) : 50;
    const uint32_t totalDur = (stepMs > 0) ? (stepMs * LED_COUNT) : 300;
    const float pos = (totalDur > 0 && stepMs > 0) ? (static_cast<float>(nowMs % totalDur) / static_cast<float>(stepMs)) : 0.0f;
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
      float dist = fabsf(static_cast<float>(i) - pos);
      if (dist > static_cast<float>(LED_COUNT) / 2.0f) {
        dist = static_cast<float>(LED_COUNT) - dist;
      }
      float intensity = 0.0f;
      if (dist < 1.0f) {
        intensity = 1.0f - (dist * 0.7f);
      } else if (dist < 1.6f) {
        intensity = 0.3f * (1.6f - dist) / 0.6f;
      }
      const uint8_t factor = static_cast<uint8_t>(intensity * 255.0f);
      logicalPixels_[i] = (factor > 0) ? scaleColorGamma(cfg.ledColors[i], factor) : RgbColor{0, 0, 0};
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
bool LedEffectEngine::renderRpmBar(uint32_t nowMs, const PulseShiftConfig& cfg,
                                   uint16_t rpm, bool benchTest) {
  if (benchTest != barFromBenchTest_ || rpm == 0) resetRpmLatches();
  barFromBenchTest_ = benchTest;
  if (rpm == 0) {
    setAllPixels(RgbColor{});
    return false;
  }
  const uint16_t hyst = cfg.rpmHysteresis;

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
  } else {
    warningLatched_ = false;
  }
  return warningLatched_;
}

bool LedEffectEngine::update(uint32_t nowMs, const PulseShiftConfig& cfg,
                             const LiveRpmSnapshot& rpm, const LedFrameInput& input) {
  // New startup settings take effect immediately, even if the previous
  // one-shot sequence completed long ago. No reset of RPM hysteresis.
  if (observedStartupAnimation_ == 0xFF) {
    // First update after reset() must keep its original boot start timestamp.
    observedStartupAnimation_ = cfg.startupAnimation;
    observedStartupDurationMs_ = cfg.startupDurationMs;
  } else if (cfg.startupAnimation != observedStartupAnimation_ ||
             cfg.startupDurationMs != observedStartupDurationMs_) {
    observedStartupAnimation_ = cfg.startupAnimation;
    observedStartupDurationMs_ = cfg.startupDurationMs;
    startupStartMs_ = nowMs;
    startupComplete_ = false;
  }

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

  // ---- Priority 3: Master power switch ---------------------------------
  // Explicit master power-off semantics: when turned off, strip is dark.
  // Hardware safety protections (P1 fault and P2 feedback) win over power-off.
  if (!cfg.isPowerOn) {
    activeLayer_ = LedActiveLayer::Off;
    setAllPixels(RgbColor{0, 0, 0});
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 4: Authorized bench-test preview -----------------------
  // An explicitly requested synthetic bench test takes precedence over
  // startup animations and owner commissioning indication, ensuring the
  // authorized bench test runs for its intended visible duration without
  // being silently obscured by cyan pulse or boot wipes.
  if (input.testRpmActive) {
    activeLayer_ = LedActiveLayer::BenchTest;
    startupComplete_ = true; // Mark startup complete so test expiration returns to normal operation
    renderRpmBar(nowMs, cfg, input.testRpm, true);
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 5: RPM validity safeguard ------------------------------
  // Effect 4 is the protected, calibrated shift light. Effect 5 is a
  // separate, explicitly UNVERIFIED diagnostic preview of the live CAN signal.
  // Neither mode is allowed to display stale, unavailable or invalid RPM.
  // The missing-signal safeguard blanks the strip when an RPM mode is active,
  // EXCEPT when the owner commissioning window is open (which displays cyan pairing pulse)
  // or when an initial provisional preview startup animation is playing.
  const bool verifiedBar = cfg.effect == 4;
  const bool sampleQualified = rpm.available && rpm.fresh && rpm.valid &&
      (!verifiedBar || (cfg.calibrationVerified && rpm.calibrationVerified &&
                        rpm.calibrationMatchesSettings));
  const bool rpmMode = verifiedBar || cfg.effect == 5;
  const bool liveRpmActive = rpmMode && sampleQualified && rpm.engineRunning;

  const bool commissioningActive = input.authWindowOpen && !liveRpmActive;
  if (!rpmMode || !sampleQualified) resetRpmLatches();

  // ---- Priority 6: owner commissioning indication ----------------------
  // Displayed when the physical authorization window is open and the engine
  // is NOT actively running with qualified RPM. If the engine is running on an
  // RPM display mode, live RPM rendering takes precedence so a rider tapping
  // or holding the button is never blinded by cyan.
  if (commissioningActive) {
    activeLayer_ = LedActiveLayer::Commissioning;
    const uint32_t phase = nowMs % 1000;
    const uint8_t factor = (phase < 500)
        ? static_cast<uint8_t>(phase * 255 / 500)
        : static_cast<uint8_t>((1000 - phase) * 255 / 500);
    const RgbColor cyan = scaleColor(RgbColor{0, 255, 255}, factor);
    setAllPixels(cyan);
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 7: startup indication ----------------------------------
  const bool loopStartup = (cfg.startupDurationMs & 0x8000u) != 0 && cfg.startupAnimation != 0;
  // Never mask active engine RPM or its missing-signal safeguard with a
  // looping decorative sequence. These layers retain priority during rides.
  const bool rpmPriority = loopStartup && rpmMode &&
      (rpm.engineRunning || !sampleQualified);
  if ((!startupComplete_ || loopStartup) && (!verifiedBar || sampleQualified) && !rpmPriority) {
    renderStartup(nowMs, cfg);
    if (!startupComplete_) {
      activeLayer_ = LedActiveLayer::Startup;
      return finishFrame(nowMs, cfg);
    }
  }

  // ---- Priority 8: verified RPM bar / provisional diagnostic preview ---
  if (rpmMode) {
    if (!sampleQualified) {
      activeLayer_ = LedActiveLayer::SafeguardBlank;
      setAllPixels(RgbColor{});
    } else {
      const bool warning = renderRpmBar(nowMs, cfg, rpm.rpm, false);
      activeLayer_ = warning ? LedActiveLayer::Warning
                             : (verifiedBar ? LedActiveLayer::RpmBar : LedActiveLayer::ProvisionalRpm);
    }
    return finishFrame(nowMs, cfg);
  }

  // ---- Priority 9: manual decorative lighting --------------------------
  activeLayer_ = LedActiveLayer::Decorative;
  renderEffect(nowMs, cfg);
  return finishFrame(nowMs, cfg);
}

bool LedEffectEngine::finishFrame(uint32_t nowMs, const PulseShiftConfig& cfg) {
  // Apply LED ordering (direction) & master brightness scaling, then detect
  // the change against the previously calculated frame buffer.
  // Physical write scheduling and confirmed state management are owned by LedController.
  bool changed = false;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const uint8_t srcIdx = (cfg.ledDirection == 1) ? (LED_COUNT - 1 - i) : i;
    outputPixels_[i] = scaleColor(logicalPixels_[srcIdx], cfg.brightness);
    if (outputPixels_[i] != lastCalculatedOutputPixels_[i]) changed = true;
  }
  if (changed) {
    memcpy(lastCalculatedOutputPixels_, outputPixels_, sizeof(outputPixels_));
  }
  (void)nowMs;
  return changed;
}
