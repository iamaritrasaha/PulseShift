// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "config.h"
#include "led_effects.h"
#include <Adafruit_NeoPixel.h>

// ============================================================================
// LedController - sole owner of the six WS2812B pixels.
//
// There is no screen/display abstraction anywhere in this class or in the
// LED engine it drives. The controller's entire job is:
//   1. own the NeoPixel bus,
//   2. per-frame, ask LedEffectEngine (the single priority arbiter) to render
//      the authoritative frame from the same LiveRpmSnapshot that BLE reports,
//   3. push the frame to the hardware only when the visible output changed.
//
// The controller is fully nonblocking and never delays CAN reception: a frame
// render is integer math into a 6-pixel buffer; latching/showing happens only
// on change (WS2812B latch takes ~50 us per refresh at most).
// ============================================================================
class LedController {
 public:
  bool begin();
  // Render one frame and flush to hardware when the output changed.
  void update(const LiveRpmSnapshot& rpm, uint32_t now, bool healthy);

  void triggerButtonFeedback(uint32_t now) {
    feedbackStartMs_ = now;
    feedbackActive_ = true;
  }

  void setTestRpm(uint16_t rpm, bool active) {
    testRpm_ = rpm;
    testRpmActive_ = active;
  }
  bool isTestRpmActive() const { return testRpmActive_; }

  LedEffectEngine& ledEffects() { return engine_; }
  const LedEffectEngine& ledEffects() const { return engine_; }

 private:
  Adafruit_NeoPixel leds_{config::ledCount, config::ledPin, NEO_GRB + NEO_KHZ800};
  LedEffectEngine engine_{};
  bool ready_ = false;
  uint32_t feedbackStartMs_ = 0;
  bool feedbackActive_ = false;
  uint16_t testRpm_ = 0;
  bool testRpmActive_ = false;
};
