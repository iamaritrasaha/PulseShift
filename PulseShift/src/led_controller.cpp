// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_controller.h"
#include "config_manager.h"
#include <Arduino.h>

bool LedController::begin() {
  leds_.begin();
  // Master brightness scaling is computed by LedEffectEngine from the
  // persisted configuration; the NeoPixel library scaler stays at full scale
  // so a saved brightness survives without a second hardware write path.
  leds_.setBrightness(255);
  leds_.clear();
  leds_.show();
  // Start the boot animation clock after peripheral setup, not at reset time.
  engine_.reset(millis());
  ready_ = true;
  return true;
}

void LedController::update(const LiveRpmSnapshot& rpm, uint32_t now, bool healthy) {
  if (!ready_) return;

  // Button press acknowledgment expires after 200 ms.
  if (feedbackActive_ && uint32_t(now - feedbackStartMs_) >= 200) {
    feedbackActive_ = false;
  }

  LedFrameInput input;
  input.canHealthy = healthy;
  input.feedbackActive = feedbackActive_;
  input.authWindowOpen = ConfigManager::instance().isAuthWindowOpen(now);
  input.testRpmActive = testRpmActive_;
  input.testRpm = testRpm_;

  const bool ledsChanged = engine_.update(now, ConfigManager::instance().active(), rpm, input);
  if (ledsChanged) {
    for (uint8_t i = 0; i < 6; ++i) {
      const RgbColor c = engine_.getPixel(i);
      leds_.setPixelColor(i, leds_.Color(c.r, c.g, c.b));
    }
    leds_.show();
  }
}
