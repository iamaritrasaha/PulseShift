// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_controller.h"
#include "config_manager.h"
#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

LedController::LedController() {
  driver_ = &defaultDriver_;
}

bool LedController::begin() {
  if (!driver_) return false;

  ready_ = driver_->begin(config::ledPin, LED_COUNT);

  // Clear all states to black
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    calculatedPixels_[i] = RgbColor{0, 0, 0};
    lastAttemptedPixels_[i] = RgbColor{0, 0, 0};
    lastConfirmedPixels_[i] = RgbColor{0, 0, 0};
  }

  // Initial flush: command all pixels black to ensure clear initial state
  if (ready_) {
    const bool ok = driver_->write(lastAttemptedPixels_, LED_COUNT);
    lastAttemptMs_ = 0;
    ++attemptsCount_;
    if (ok) {
      hasConfirmedFrame_ = true;
      lastConfirmedMs_ = 0;
      ++successCount_;
    } else {
      ++failureCount_;
    }
  }

  // Start the boot animation clock after peripheral setup, not at reset time.
#if defined(ARDUINO)
  engine_.reset(millis());
#else
  engine_.reset(0);
#endif

  return ready_;
}

void LedController::update(const LiveRpmSnapshot& rpm, uint32_t now, bool healthy) {
  if (!ready_ || !driver_) return;

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

  // 1. Calculate authoritative frame into engine
  engine_.update(now, ConfigManager::instance().active(), rpm, input);

  // 2. Fetch the newly calculated pixels
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    calculatedPixels_[i] = engine_.getPixel(i);
  }

  // 3. Determine if physical transmission is required:
  //    a) Calculated state changed relative to last confirmed physical state
  //    b) Retry due after previous write failure (50 ms backoff)
  //    c) Periodic refresh interval elapsed (1000 ms forced resend to prevent freezing)
  const bool pixelsChanged = !hasConfirmedFrame_ ||
      (memcmp(calculatedPixels_, lastConfirmedPixels_, sizeof(calculatedPixels_)) != 0);

  const bool retryDue = (!hasConfirmedFrame_ || (lastAttemptMs_ > lastConfirmedMs_)) &&
      (uint32_t(now - lastAttemptMs_) >= RETRY_INTERVAL_MS);

  const bool periodicRefreshDue = hasConfirmedFrame_ &&
      (uint32_t(now - lastConfirmedMs_) >= PERIODIC_REFRESH_INTERVAL_MS);

  if (pixelsChanged || retryDue || periodicRefreshDue) {
    if (!pixelsChanged && periodicRefreshDue) {
      ++forcedRefreshCount_;
    }

    memcpy(lastAttemptedPixels_, calculatedPixels_, sizeof(lastAttemptedPixels_));
    lastAttemptMs_ = now;
    ++attemptsCount_;

    const bool ok = driver_->write(lastAttemptedPixels_, LED_COUNT);
    if (ok) {
      memcpy(lastConfirmedPixels_, lastAttemptedPixels_, sizeof(lastConfirmedPixels_));
      lastConfirmedMs_ = now;
      hasConfirmedFrame_ = true;
      ++successCount_;
    } else {
      ++failureCount_;
      // lastConfirmedPixels_ is NOT modified on failure.
      // Next tick will detect unconfirmed change or retryDue, triggering automatic retry.
    }
  }
}

#if PULSESHIFT_SERIAL_DIAGNOSTICS
void LedController::printStatus(uint32_t now) const {
  Serial.println("\n--- Physical WS2812B LED Output Subsystem ---");
  Serial.printf("Hardware: GPIO%u | Pixel Count: %u | Driver: %s | Status: %s\r\n",
                (unsigned)config::ledPin, (unsigned)LED_COUNT,
#if defined(ARDUINO) && defined(ESP32)
                "ESP32-RMT-HAL (4 blocks/256 sym)",
#else
                "MockDriver",
#endif
                lastDriverStatusString());

  const char* layerName = "OFF";
  switch (engine_.activeLayer()) {
    case LedActiveLayer::Off: layerName = "OFF (Master Power Switch)"; break;
    case LedActiveLayer::Fault: layerName = "FAULT (CAN Controller Halted - Red Blink)"; break;
    case LedActiveLayer::Feedback: layerName = "FEEDBACK (Button Interaction Flash)"; break;
    case LedActiveLayer::SafeguardBlank: layerName = "SAFEGUARD_BLANK (Unverified/Invalid RPM)"; break;
    case LedActiveLayer::Commissioning: layerName = "COMMISSIONING (Auth Window Cyan Pulse)"; break;
    case LedActiveLayer::Startup: layerName = "STARTUP (Boot Wipe/Wave Animation)"; break;
    case LedActiveLayer::RpmBar: layerName = "RPM_BAR (Verified Shift Light)"; break;
    case LedActiveLayer::BenchTest: layerName = "BENCH_TEST (Active Bench RPM Test)"; break;
    case LedActiveLayer::Decorative: layerName = "DECORATIVE (Configured Effect)"; break;
    case LedActiveLayer::Warning: layerName = "WARNING (RPM Shift Warning Overlay)"; break;
    case LedActiveLayer::ProvisionalRpm: layerName = "PROVISIONAL_RPM (Diagnostic Preview)"; break;
  }
  Serial.printf("Active Layer: %s\r\n", layerName);
  Serial.printf("Bench Test: %s | Test RPM: %u\r\n",
                testRpmActive_ ? "ACTIVE" : "INACTIVE", (unsigned)testRpm_);
  Serial.printf("Transmissions: Attempts=%lu | Confirmed=%lu | Failures=%lu | ForcedRefreshes=%lu\r\n",
                (unsigned long)attemptsCount_, (unsigned long)successCount_,
                (unsigned long)failureCount_, (unsigned long)forcedRefreshCount_);
  Serial.printf("Last Attempt: %lu ms ago | Last Confirmed: %s\r\n",
                hasConfirmedFrame_ ? (unsigned long)(now - lastAttemptMs_) : 0,
                hasConfirmedFrame_ ? "YES" : "NO");

  Serial.print("Calculated Frame (RGB): ");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    Serial.printf("[%u]=(%u,%u,%u) ", i, calculatedPixels_[i].r, calculatedPixels_[i].g, calculatedPixels_[i].b);
  }
  Serial.println();

  Serial.print("Confirmed Frame  (RGB): ");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    Serial.printf("[%u]=(%u,%u,%u) ", i, lastConfirmedPixels_[i].r, lastConfirmedPixels_[i].g, lastConfirmedPixels_[i].b);
  }
  Serial.println();
}
#endif
