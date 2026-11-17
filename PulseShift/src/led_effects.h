// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "led_color.h"       // RgbColor / scaleColor
#include "rpm_candidate.h"   // LiveRpmSnapshot (authoritative RPM state)
#include "config_manager.h"

// ============================================================================
// Unified LED controller - the SINGLE writer for all six WS2812B pixels.
//
// Every frame is produced by one priority arbiter so no two LED engines can
// write contradictory output during a single refresh:
//
//   1. Hardware fault / safety state      (CAN halted: red blink)
//   2. Button feedback flash              (physical interaction acknowledgment)
//   3. Master power switch                (explicit off)
//   4. Bench test                         (synthetic RPM)
//   5. Owner commissioning                (auth window, no qualified running RPM)
//   6. Startup indication                 (configured boot animation)
//   7. Live RPM with validity safeguards  (calibrated or provisional)
//   8. Decorative lighting               (static/breathing/rainbow/chase)
//
// The safety and power layers govern cosmetic effects, and live RPM-driven
// lighting renders from the same authoritative
// LiveRpmSnapshot that BLE telemetry reports.
//
// The controller is fully nonblocking: update() renders one frame into an
// internal buffer and returns whether the visible output changed.
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
  ProvisionalRpm = 10 // P6 unverified diagnostic preview; NOT a calibrated shift cue
};

// Per-frame inputs that do not belong to the persisted configuration.
struct LedFrameInput {
  bool     canHealthy = true;      // P1: false => CAN controller halted/fault
  bool     feedbackActive = false; // P2: 200 ms white flash after button press
  bool     authWindowOpen = false; // P4: physical owner commissioning window
  bool     testRpmActive = false;  // P6b: bench-test preview active
  uint16_t testRpm = 0;
};

class LedEffectEngine {
 public:
  static constexpr uint8_t LED_COUNT = 6;

  LedEffectEngine();

  // Restart the startup animation clock (called once from Dashboard::begin).
  void reset(uint32_t nowMs);

  // Render one frame. Returns true when the visible output changed.
  bool update(uint32_t nowMs, const PulseShiftConfig& cfg,
              const LiveRpmSnapshot& rpm, const LedFrameInput& input);

  RgbColor getPixel(uint8_t index) const {
    if (index >= LED_COUNT) return RgbColor{0, 0, 0};
    return outputPixels_[index];
  }

  bool isStartupComplete() const { return startupComplete_; }
  LedActiveLayer activeLayer() const { return activeLayer_; }

 private:
  void renderStartup(uint32_t nowMs, const PulseShiftConfig& cfg);
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
  bool barFromBenchTest_ = false;

  // Per-LED hysteresis latch: LED i stays lit until rpm falls below
  // thresholds[i] - hysteresis, preventing flicker around a threshold.
  bool barLatched_[LED_COUNT] = {};
  // Warning latch with its own hysteresis so the alert does not chatter
  // around the warning threshold.
  bool warningLatched_ = false;
  uint32_t warningStartMs_ = 0;

  LedActiveLayer activeLayer_ = LedActiveLayer::Off;

  RgbColor logicalPixels_[LED_COUNT]{};
  RgbColor outputPixels_[LED_COUNT]{};
  RgbColor lastCalculatedOutputPixels_[LED_COUNT]{};
};
