// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_effects.h"
#include <string.h>
#include <math.h>

namespace {
constexpr uint16_t WARNING_FLASH_PERIOD_MS = 100;   // 10 Hz flash
constexpr uint16_t WARNING_STROBE_PERIOD_MS = 50;   // 20 Hz strobe
}  // namespace

void LedEffectEngine::reset(uint32_t nowMs, uint8_t pixelCount) {
  pixelCount_ = pixelCount >= config::minPhysicalLeds && pixelCount <= config::maxPhysicalLeds
      ? pixelCount : config::ledCount;
  startupStartMs_ = nowMs;
  startupComplete_ = false;
  activeLayer_ = LedActiveLayer::Off;
  canEvidenceSeen_ = false;
  engineWasRunning_ = false;
  animationEligible_ = false;
  lastBarCount_ = 0;
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
  for (uint8_t i = 0; i < renderCount_; ++i) {
    logicalPixels_[i] = color;
  }
}

void LedEffectEngine::renderAnimation(uint8_t pattern, uint32_t dur, uint32_t elapsed,
                                      const PulseShiftConfig& cfg, bool ambient) {
  // Animation 1: Sweep
  if (pattern == 1) {
    const uint32_t half = dur / 2;
    if (elapsed < half) {
      // Forward wipe
      const uint32_t activeIdx = elapsed * renderCount_ / half;
      for (uint8_t i = 0; i < renderCount_; ++i) {
        logicalPixels_[i] = (i <= activeIdx) ? cfg.ledColors[colorZone(i)] : RgbColor{0, 0, 0};
      }
    } else {
      // Fade into active color
      const uint32_t dt = elapsed - half;
      const uint8_t factor = static_cast<uint8_t>(255 - (dt * 155 / half));
      for (uint8_t i = 0; i < renderCount_; ++i) {
        logicalPixels_[i] = scaleColor(cfg.ledColors[colorZone(i)], factor);
      }
    }
  }
  // Animation 2: Wave (Dual-crested spatial wave)
  else if (pattern == 2) {
    for (uint8_t i = 0; i < renderCount_; ++i) {
      const float phase = (elapsed * 6.28318f / dur) + (i * 6.28318f / renderCount_);
      const float sinVal = (sinf(phase) + 1.0f) * 0.5f; // 0.0 .. 1.0
      const uint8_t brightness = static_cast<uint8_t>(sinVal * 255.0f);
      logicalPixels_[i] = scaleColor(cfg.ledColors[colorZone(i)], brightness);
    }
  }
  // Animation 3: Pulse (Heartbeat dual pulse)
  else if (pattern == 3) {
    const uint32_t phaseMs = elapsed % (dur / 2);
    const uint32_t pulseDur = dur / 4;
    uint8_t factor = 0;
    if (phaseMs < pulseDur) {
      factor = (phaseMs < pulseDur / 2)
          ? static_cast<uint8_t>(phaseMs * 255 / (pulseDur / 2))
          : static_cast<uint8_t>((pulseDur - phaseMs) * 255 / (pulseDur / 2));
    }
    for (uint8_t i = 0; i < renderCount_; ++i) {
      logicalPixels_[i] = scaleColor(cfg.ledColors[colorZone(i)], factor);
    }
  }
  // Animation 4: Center-Out, expanding symmetrically one ring at a time.
  else if (pattern == 4) {
    const uint8_t rings = (renderCount_ + 1) / 2;
    const uint8_t visibleRings = elapsed * rings / dur + 1;
    for (uint8_t i = 0; i < renderCount_; ++i) {
      const uint8_t opposite = renderCount_ - 1 - i;
      const uint8_t ring = rings - 1 - (i < opposite ? i : opposite);
      const uint8_t factor = ambient ? static_cast<uint8_t>(
          255 - (elapsed * 255 / dur)) : 255;
      logicalPixels_[i] = ring < visibleRings ?
          scaleColor(cfg.ledColors[colorZone(i)], factor) : RgbColor{};
    }
  }
  // Animation 5: Strobe Rush (F1 Grid Start - multi-burst sequence transitioning to full solid)
  else if (pattern == 5) {
    const uint32_t flashPeriod = (dur > 6) ? (dur / 6) : 1;
    const bool flashOn = (elapsed >= (dur * 2) / 3) || ((elapsed / flashPeriod) % 2 == 0);
    for (uint8_t i = 0; i < renderCount_; ++i) {
      logicalPixels_[i] = flashOn ? cfg.ledColors[colorZone(i)] : RgbColor{0, 0, 0};
    }
  }
}

void LedEffectEngine::renderEffect(uint32_t nowMs, const PulseShiftConfig& cfg) {
  // Effect 0: Static Color
  if (cfg.effect == 0) {
    for (uint8_t i = 0; i < renderCount_; ++i) {
      logicalPixels_[i] = cfg.ledColors[colorZone(i)];
    }
  }
  // Effect 1: Breathing (Perceptually smooth sinusoidal pulse)
  else if (cfg.effect == 1) {
    const uint32_t period = (cfg.effectSpeed > 0) ? (4000 - (cfg.effectSpeed * 32)) : 4000;
    const float phase = (period > 0) ? ((nowMs % period) * 6.2831853f / period) : 0.0f;
    const float sinVal = (sinf(phase) + 1.0f) * 0.45f + 0.1f; // 0.1 .. 1.0
    const uint8_t factor = static_cast<uint8_t>(sinVal * 255.0f);
    for (uint8_t i = 0; i < renderCount_; ++i) {
      logicalPixels_[i] = scaleColorGamma(cfg.ledColors[colorZone(i)], factor);
    }
  }
  // Effect 2: Rainbow (Smooth full-spectrum HSV color wave)
  else if (cfg.effect == 2) {
    // Speed: 0..100 maps to hue rate
    const uint32_t speedFactor = (cfg.effectSpeed > 0) ? cfg.effectSpeed : 10;
    const uint8_t baseHue = static_cast<uint8_t>((nowMs % (40 * 256)) * speedFactor / 40);
    for (uint8_t i = 0; i < renderCount_; ++i) {
      const uint8_t hue = (baseHue + (i * 256 / renderCount_)) % 256;
      logicalPixels_[i] = hsvToRgb(hue, 255, 255);
    }
  }
  // Effect 3: Color Chase (Smooth runner traversing LEDs with anti-aliasing)
  else if (cfg.effect == 3) {
    const uint32_t stepMs = (cfg.effectSpeed <= 100) ? (350 - (cfg.effectSpeed * 3)) : 50;
    const uint32_t totalDur = (stepMs > 0) ? (stepMs * renderCount_) : 300;
    const float pos = (totalDur > 0 && stepMs > 0) ? (static_cast<float>(nowMs % totalDur) / static_cast<float>(stepMs)) : 0.0f;
    for (uint8_t i = 0; i < renderCount_; ++i) {
      float dist = fabsf(static_cast<float>(i) - pos);
      if (dist > static_cast<float>(renderCount_) / 2.0f) {
        dist = static_cast<float>(renderCount_) - dist;
      }
      float intensity = 0.0f;
      if (dist < 1.0f) {
        intensity = 1.0f - (dist * 0.7f);
      } else if (dist < 1.6f) {
        intensity = 0.3f * (1.6f - dist) / 0.6f;
      }
      const uint8_t factor = static_cast<uint8_t>(intensity * 255.0f);
      logicalPixels_[i] = (factor > 0) ? scaleColorGamma(cfg.ledColors[colorZone(i)], factor) : RgbColor{0, 0, 0};
    }
  }
  else {
    // Unknown effect id: fall back to static.
    for (uint8_t i = 0; i < renderCount_; ++i) {
      logicalPixels_[i] = cfg.ledColors[colorZone(i)];
    }
  }
}

// Bounded RPM bar with per-LED hysteresis. LED i activates when the
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

  uint16_t thresholds[config::maxPhysicalLeds];
  cfg.rpmMapping.expand(thresholds, renderCount_, cfg.rpmThresholds);
  const size_t bytes = renderCount_ * sizeof(uint16_t);
  if (lastBarCount_ != renderCount_ || lastHysteresis_ != hyst ||
      lastWarningRpm_ != cfg.warningRpm || lastWarningPattern_ != cfg.warningPatternId ||
      memcmp(lastThresholds_, thresholds, bytes) != 0) {
    resetRpmLatches();
    memcpy(lastThresholds_, thresholds, bytes);
    lastBarCount_ = renderCount_;
    lastHysteresis_ = hyst;
    lastWarningRpm_ = cfg.warningRpm;
    lastWarningPattern_ = cfg.warningPatternId;
  }
  for (uint8_t i = 0; i < renderCount_; ++i) {
    const uint16_t onAt = thresholds[i];
    const uint16_t offBelow = (onAt > hyst) ? static_cast<uint16_t>(onAt - hyst) : 0;
    barLatched_[i] = barLatched_[i] ? rpm >= offBelow : rpm >= onAt;
    logicalPixels_[i] = barLatched_[i] ? cfg.ledColors[colorZone(i)] : RgbColor{};
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
  const uint8_t limit = pixelCount_ < cfg.installedLedCount ? pixelCount_ : cfg.installedLedCount;
  renderCount_ = cfg.rpmMapping.activeCount(limit);
  const bool verifiedBar = cfg.effect == 4;
  const bool rpmMode = verifiedBar || cfg.effect == 5;
  const bool decoded = rpm.available && rpm.fresh && rpm.valid;
  const bool qualified = decoded && (!verifiedBar ||
      (cfg.calibrationVerified && rpm.calibrationVerified && rpm.calibrationMatchesSettings));
  canEvidenceSeen_ |= input.canTrafficSeen || rpm.available;
  if (decoded) engineWasRunning_ = rpm.engineRunning;
  const bool running = decoded && rpm.engineRunning;
  const bool stopped = qualified && !rpm.engineRunning;
  const bool initiallyQuiet = !canEvidenceSeen_;
  const bool safeIdle = initiallyQuiet || stopped;
  const bool safeguard = rpmMode && !qualified && !initiallyQuiet;
  const bool liveRpm = rpmMode && qualified && running;
  const uint32_t bootElapsed = nowMs - startupStartMs_;
  startupComplete_ |= !cfg.bootEnabled || cfg.startupAnimation == 0 ||
      bootElapsed >= cfg.startupDurationMs || running || input.testRpmActive;

  animationEligible_ = input.canHealthy && cfg.isPowerOn && safeIdle &&
      !input.feedbackActive && !input.testRpmActive && !input.authWindowOpen;
  ambientStatus_ = AmbientStatus::Off;
  if (cfg.ambient.enabled && cfg.isPowerOn) {
    if (!input.canHealthy || (!running && !safeIdle) || safeguard) {
      ambientStatus_ = AmbientStatus::SuppressedBySafety;
    } else if (running) {
      ambientStatus_ = AmbientStatus::PausedByRpm;
    } else {
      ambientStatus_ = AmbientStatus::Waiting;
    }
  }

  const LedActiveLayer previous = activeLayer_;
  if (!liveRpm && !input.testRpmActive) resetRpmLatches();

  if (!input.canHealthy) {
    activeLayer_ = LedActiveLayer::Fault;
    setAllPixels((nowMs / 100) % 2 == 0 ? RgbColor{255, 0, 0} : RgbColor{});
  } else if (input.feedbackActive) {
    activeLayer_ = LedActiveLayer::Feedback;
    setAllPixels(RgbColor{255, 255, 255});
  } else if (!cfg.isPowerOn) {
    activeLayer_ = LedActiveLayer::Off;
    setAllPixels(RgbColor{});
  } else if (input.testRpmActive) {
    activeLayer_ = LedActiveLayer::BenchTest;
    renderRpmBar(nowMs, cfg, input.testRpm, true);
  } else if (liveRpm) {
    const bool warning = renderRpmBar(nowMs, cfg, rpm.rpm, false);
    activeLayer_ = warning ? LedActiveLayer::Warning :
        (verifiedBar ? LedActiveLayer::RpmBar : LedActiveLayer::ProvisionalRpm);
  } else if (input.authWindowOpen && !engineWasRunning_) {
    activeLayer_ = LedActiveLayer::Commissioning;
    const uint32_t phase = nowMs % 1000;
    const uint32_t level = phase < 500 ? phase : 1000 - phase;
    setAllPixels(scaleColor(RgbColor{0, 255, 255}, static_cast<uint8_t>(level * 255 / 500)));
  } else if (safeguard) {
    activeLayer_ = LedActiveLayer::SafeguardBlank;
    setAllPixels(RgbColor{});
  } else if (animationEligible_ && input.previewPattern != 0) {
    activeLayer_ = LedActiveLayer::AnimationPreview;
    renderAnimation(input.previewPattern, input.previewDurationMs, input.previewElapsedMs, cfg, false);
  } else if (!startupComplete_ && safeIdle) {
    activeLayer_ = LedActiveLayer::Startup;
    renderAnimation(cfg.startupAnimation, cfg.startupDurationMs, bootElapsed, cfg, false);
  } else if (cfg.ambient.enabled && animationEligible_) {
    activeLayer_ = LedActiveLayer::Ambient;
    ambientStatus_ = AmbientStatus::Playing;
    if (previous != LedActiveLayer::Ambient || ambientPattern_ != cfg.ambient.pattern ||
        ambientCycleMs_ != cfg.ambient.cycleMs) {
      ambientPhaseMs_ = 0;
      ambientPattern_ = cfg.ambient.pattern;
      ambientCycleMs_ = cfg.ambient.cycleMs;
    } else {
      const uint32_t elapsed = nowMs - ambientLastMs_;
      ambientPhaseMs_ = (ambientPhaseMs_ + elapsed % ambientCycleMs_) % ambientCycleMs_;
    }
    ambientLastMs_ = nowMs;
    renderAnimation(ambientPattern_, ambientCycleMs_, ambientPhaseMs_, cfg, true);
  } else if (rpmMode) {
    activeLayer_ = qualified ? (verifiedBar ? LedActiveLayer::RpmBar : LedActiveLayer::ProvisionalRpm)
                             : LedActiveLayer::SafeguardBlank;
    setAllPixels(RgbColor{});
  } else {
    activeLayer_ = LedActiveLayer::Decorative;
    renderEffect(nowMs, cfg);
  }
  return finishFrame(nowMs, cfg);
}

bool LedEffectEngine::finishFrame(uint32_t nowMs, const PulseShiftConfig& cfg) {
  // Apply LED ordering (direction) & master brightness scaling, then detect
  // the change against the previously calculated frame buffer.
  // Physical write scheduling and confirmed state management are owned by LedController.
  bool changed = false;
  for (uint8_t i = 0; i < pixelCount_; ++i) {
    const uint8_t srcIdx = (cfg.ledDirection == 1) ? (pixelCount_ - 1 - i) : i;
    outputPixels_[i] = srcIdx < renderCount_ ? scaleColor(logicalPixels_[srcIdx], cfg.brightness) : RgbColor{};
    if (outputPixels_[i] != lastCalculatedOutputPixels_[i]) changed = true;
  }
  if (changed) {
    memcpy(lastCalculatedOutputPixels_, outputPixels_, pixelCount_ * sizeof(RgbColor));
  }
  (void)nowMs;
  return changed;
}
