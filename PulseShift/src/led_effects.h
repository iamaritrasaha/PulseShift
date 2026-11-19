// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "led_color.h"       // RgbColor / scaleColor
#include "rpm_candidate.h"   // LiveRpmSnapshot (authoritative RPM state)
#include "config_manager.h"

// ============================================================================
// One priority arbiter produces each frame from configuration, RPM and
// hardware state. The controller owns physical transmission and retries.
// ============================================================================

enum class LedActiveLayer : uint8_t {
  Off = 0,           // master power off
  Fault = 1,         // P1 hardware fault
  Feedback = 2,      // P2 button-press acknowledgment flash
  SafeguardBlank = 3,// P3 RPM validity safeguard (all dark)
  Commissioning = 4, // P4 auth window pulse
  Startup = 5,       // P5 startup animation
  RpmBar = 6,        // P6 verified RPM bar
  BenchTest = 7,     // P6 owner bench-test preview (explicit provenance)
  Decorative = 8,    // P7 manual effect
  Warning = 9,       // shift warning overlay (reported as its own layer)
  ProvisionalRpm = 10, // Unverified diagnostic preview
  Ambient = 11,
  AnimationPreview = 12
};

// Per-frame inputs that do not belong to the persisted configuration.
struct LedFrameInput {
  bool     canHealthy = true;      // P1: false => CAN controller halted/fault
  bool     feedbackActive = false; // P2: 200 ms white flash after button press
  bool     authWindowOpen = false; // P4: physical owner commissioning window
  bool     testRpmActive = false;  // P6b: bench-test preview active
  uint16_t testRpm = 0;
  bool canTrafficSeen = false;
  uint8_t previewPattern = 0;
  uint16_t previewDurationMs = 0;
  uint32_t previewElapsedMs = 0;
};

class LedEffectEngine {
 public:
  // Restart the startup animation clock (called once from Dashboard::begin).
  void reset(uint32_t nowMs, uint8_t pixelCount = config::ledCount);

  // Render one frame. Returns true when the visible output changed.
  bool update(uint32_t nowMs, const PulseShiftConfig& cfg,
              const LiveRpmSnapshot& rpm, const LedFrameInput& input);

  RgbColor getPixel(uint8_t index) const {
    if (index >= pixelCount_) return RgbColor{0, 0, 0};
    return outputPixels_[index];
  }

  bool isStartupComplete() const { return startupComplete_; }
  LedActiveLayer activeLayer() const { return activeLayer_; }
  AmbientStatus ambientStatus() const { return ambientStatus_; }
  bool animationEligible() const { return animationEligible_; }

 private:
  void renderAnimation(uint8_t pattern, uint32_t dur, uint32_t elapsed,
                       const PulseShiftConfig& cfg, bool ambient);
  void renderEffect(uint32_t nowMs, const PulseShiftConfig& cfg);
  bool renderRpmBar(uint32_t nowMs, const PulseShiftConfig& cfg, uint16_t rpm, bool benchTest);
  void resetRpmLatches();
  void setAllPixels(RgbColor color);
  // Renders logicalPixels_ into outputPixels_ (direction + brightness),
  // detects the change against the previous physical frame, records the
  // change flag and refreshes the previous-frame buffer. Returns whether the
  // visible output changed and therefore requires a physical LED write.
  bool finishFrame(uint32_t nowMs, const PulseShiftConfig& cfg);

  // Fast integer HSV to RGB converter (h: 0..255, s: 0..255, v: 0..255)
  static RgbColor hsvToRgb(uint8_t h, uint8_t s, uint8_t v);

  bool startupComplete_ = false;
  uint32_t startupStartMs_ = 0;
  bool canEvidenceSeen_ = false;
  bool engineWasRunning_ = false;
  uint32_t ambientLastMs_ = 0;
  uint16_t ambientPhaseMs_ = 0;
  uint16_t ambientCycleMs_ = 0;
  uint8_t ambientPattern_ = 0;
  AmbientStatus ambientStatus_ = AmbientStatus::Off;
  bool animationEligible_ = false;
  bool barFromBenchTest_ = false;

  // Per-LED hysteresis latch: LED i stays lit until rpm falls below
  // thresholds[i] - hysteresis, preventing flicker around a threshold.
  bool barLatched_[config::maxPhysicalLeds] = {};
  uint16_t lastThresholds_[config::maxPhysicalLeds]{};
  uint16_t lastHysteresis_ = 0;
  uint16_t lastWarningRpm_ = 0;
  uint8_t lastWarningPattern_ = 0;
  uint8_t lastBarCount_ = 0;
  // Warning latch with its own hysteresis so the alert does not chatter
  // around the warning threshold.
  bool warningLatched_ = false;
  uint32_t warningStartMs_ = 0;

  LedActiveLayer activeLayer_ = LedActiveLayer::Off;

  uint8_t colorZone(uint8_t pixel) const { return pixel * config::rpmStages / renderCount_; }

  uint8_t pixelCount_ = config::ledCount;
  uint8_t renderCount_ = config::ledCount;
  RgbColor logicalPixels_[config::maxPhysicalLeds]{};
  RgbColor outputPixels_[config::maxPhysicalLeds]{};
  RgbColor lastCalculatedOutputPixels_[config::maxPhysicalLeds]{};
};
