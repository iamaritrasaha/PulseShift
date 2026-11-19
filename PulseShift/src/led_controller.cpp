// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_controller.h"
#include "config_manager.h"
#include "diag_logger.h"
#include <stdio.h>
#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

LedController::LedController() {
  driver_ = &defaultDriver_;
}

bool LedController::begin() {
  if (!driver_) return false;
  pixelCount_ = ConfigManager::instance().runningLedCount();

#if defined(ARDUINO)
  initializeDriver(millis());
#else
  initializeDriver(0);
#endif

  // Start the boot animation clock after peripheral setup, not at reset time.
#if defined(ARDUINO)
  engine_.reset(millis(), pixelCount_);
#else
  engine_.reset(0, pixelCount_);
#endif

  return ready_;
}

void LedController::initializeDriver(uint32_t now) {
  lastInitAttemptMs_ = now;
  ++initAttempts_;
  ready_ = driver_->begin(config::ledPin, pixelCount_);
  if (!ready_) {
    recordError(now);
    return;
  }
  // Clear the strip on initial setup or recovery before resuming output.
  for (auto& pixel : lastAttemptedPixels_) pixel = RgbColor{};
  transmitAttemptedFrame(now);
}

void LedController::transmitAttemptedFrame(uint32_t now) {
  lastAttemptMs_ = now;
  ++attemptsCount_;
  if (driver_->write(lastAttemptedPixels_, pixelCount_)) {
    finishTransmission(now);
    return;
  }
  ++failureCount_;
  lastAttemptFailed_ = true;
  ready_ = driver_->isReady();
  if (!ready_) lastInitAttemptMs_ = now;
  recordError(now);
}

void LedController::finishTransmission(uint32_t now) {
  const auto status = driver_->pollTransmission();
  transmissionPending_ = status == LedDriverStatus::Busy;
  if (transmissionPending_) return;
  if (status == LedDriverStatus::TransmitOk) {
    memcpy(lastConfirmedPixels_, lastAttemptedPixels_, pixelCount_ * sizeof(RgbColor));
    lastConfirmedMs_ = now;
    hasConfirmedFrame_ = true;
    lastAttemptFailed_ = false;
    ++successCount_;
  } else {
    ++failureCount_;
    lastAttemptFailed_ = true;
    ready_ = driver_->isReady();
    if (!ready_) lastInitAttemptMs_ = now;
    recordError(now);
  }
}

void LedController::recordError(uint32_t now) {
  const bool firstError = lastError_ == LedDriverStatus::Uninitialized;
  const bool newTerminalError = driver_->isTerminalFault() && lastDriverStatus() != lastError_;
  lastError_ = lastDriverStatus();
  lastErrorMs_ = now;
  if (!firstError && !newTerminalError && uint32_t(now - lastErrorLoggedMs_) < 5000) return;
  lastErrorLoggedMs_ = now;
  char frame[config::rpmStages * 7 + 1]{};
  for (uint8_t i = 0; i < pixelCount_ && i < config::rpmStages; ++i) {
    const auto color = calculatedPixels_[i];
    snprintf(frame + i * 7, 8, "%02X%02X%02X ", (unsigned)color.r, (unsigned)color.g, (unsigned)color.b);
  }
  DiagLogger::instance().logEvent(DiagLevel::Warn, "LED_DRIVER",
      "%s attempts=%lu completed=%lu failures=%lu init=%u layer=%u desired=%s",
      lastDriverStatusString(), (unsigned long)attemptsCount_, (unsigned long)successCount_,
      (unsigned long)failureCount_, (unsigned)initAttempts_, (unsigned)engine_.activeLayer(), frame);
}

bool LedController::serviceTransmission(uint32_t now) {
  if (!driver_) return true;
  if (transmissionPending_) {
    finishTransmission(now);
  } else if (driver_->ownsPendingSymbols()) {
    driver_->pollTransmission();
    if (driver_->isTerminalFault()) {
      ready_ = false;
      recordError(now);
    }
  }
  return !driver_->ownsPendingSymbols();
}

void LedController::update(const LiveRpmSnapshot& rpm, uint32_t now, bool healthy, bool canTrafficSeen) {
  if (!driver_) return;

  // Safety, power and feedback layers can interrupt the preview. Its request
  // still expires so a synthetic RPM cannot unexpectedly resume much later.
  if (testRpmActive_ && uint32_t(now - testRpmStartMs_) >= BENCH_TEST_DURATION_MS) {
    testRpmActive_ = false;
    testRpm_ = 0;
  }

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
  input.canTrafficSeen = canTrafficSeen;
  const uint32_t previewElapsed = now - previewStartMs_;
  if (previewElapsed >= previewDurationMs_) previewPattern_ = 0;
  input.previewPattern = previewPattern_;
  input.previewDurationMs = previewDurationMs_;
  input.previewElapsedMs = previewElapsed;

  // 1. Calculate authoritative frame into engine
  engine_.update(now, ConfigManager::instance().active(), rpm, input);
  if (!engine_.animationEligible()) previewPattern_ = 0;

  // 2. Fetch the newly calculated pixels
  for (uint8_t i = 0; i < pixelCount_; ++i) {
    calculatedPixels_[i] = engine_.getPixel(i);
  }

  if (transmissionPending_) {
    finishTransmission(now);
    if (transmissionPending_) return;
  }

  if (!ready_) {
    if (!driver_->isTerminalFault() && initAttempts_ < MAX_INIT_ATTEMPTS &&
        uint32_t(now - lastInitAttemptMs_) >= INIT_RETRY_INTERVAL_MS) {
      initializeDriver(now);
    }
    return;
  }

  // 3. Transmission scheduling state machine:
  // Check if we are currently in a failure backoff window.
  // If the last transmission attempt failed, we MUST enforce RETRY_INTERVAL_MS (50 ms) backoff
  // before attempting any hardware write. This prevents failed writes from triggering
  // an unbounded busy retry on every loop tick even when pixelsChanged remains true.
  if (lastAttemptFailed_) {
    const uint32_t elapsedSinceAttempt = uint32_t(now - lastAttemptMs_);
    if (elapsedSinceAttempt < RETRY_INTERVAL_MS) {
      // Backoff active: suppress transmission until backoff interval expires
      return;
    }
  }

  // If not backed off (or backoff interval has now expired):
  // Transmission is triggered if:
  //    a) Calculated state changed relative to the last completed RMT write
  //    b) Retry due after previous write failure whose backoff has elapsed
  //    c) Periodic refresh interval elapsed (1000 ms forced resend to prevent freezing)
  const bool pixelsChanged = !hasConfirmedFrame_ ||
      (memcmp(calculatedPixels_, lastConfirmedPixels_, pixelCount_ * sizeof(RgbColor)) != 0);

  const bool retryDue = lastAttemptFailed_;

  const bool periodicRefreshDue = hasConfirmedFrame_ &&
      (uint32_t(now - lastConfirmedMs_) >= PERIODIC_REFRESH_INTERVAL_MS);

  if (pixelsChanged || retryDue || periodicRefreshDue) {
    if (!pixelsChanged && periodicRefreshDue) {
      ++forcedRefreshCount_;
    }

    memcpy(lastAttemptedPixels_, calculatedPixels_, pixelCount_ * sizeof(RgbColor));
    transmitAttemptedFrame(now);
  }
}

#if PULSESHIFT_SERIAL_DIAGNOSTICS
void LedController::printStatus(uint32_t now) const {
  Serial.println("\n--- Physical WS2812B LED Output Subsystem ---");
  Serial.printf("Hardware: GPIO%u | Pixel Count: %u | Driver: %s | Status: %s\r\n",
                (unsigned)config::ledPin, (unsigned)pixelCount_,
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
    case LedActiveLayer::Ambient: layerName = "AMBIENT"; break;
    case LedActiveLayer::AnimationPreview: layerName = "ANIMATION_PREVIEW"; break;
  }
  Serial.printf("Active Layer: %s\r\n", layerName);
  Serial.printf("Bench Test: %s | Test RPM: %u\r\n",
                testRpmActive_ ? "ACTIVE" : "INACTIVE", (unsigned)testRpm_);
  Serial.printf("Transmissions: Attempts=%lu | RMT Completed=%lu | Failures=%lu | ForcedRefreshes=%lu\r\n",
                (unsigned long)attemptsCount_, (unsigned long)successCount_,
                (unsigned long)failureCount_, (unsigned long)forcedRefreshCount_);
  if (attemptsCount_ != 0) Serial.printf("Last Attempt: %lu ms ago\r\n", (unsigned long)(now - lastAttemptMs_));
  if (hasConfirmedFrame_) Serial.printf("Last RMT Completion: %lu ms ago\r\n", (unsigned long)(now - lastConfirmedMs_));
  if (lastError_ != LedDriverStatus::Uninitialized) {
    Serial.printf("Last Driver Error: %u (%lu ms ago)\r\n", (unsigned)lastError_, (unsigned long)(now - lastErrorMs_));
  }

  Serial.print("Calculated Frame (RGB): ");
  for (uint8_t i = 0; i < pixelCount_; ++i) {
    Serial.printf("[%u]=(%u,%u,%u) ", i, calculatedPixels_[i].r, calculatedPixels_[i].g, calculatedPixels_[i].b);
  }
  Serial.println();

  Serial.print("Last Attempted Frame (RGB): ");
  for (uint8_t i = 0; i < pixelCount_; ++i) {
    Serial.printf("[%u]=(%u,%u,%u) ", i, lastAttemptedPixels_[i].r, lastAttemptedPixels_[i].g, lastAttemptedPixels_[i].b);
  }
  Serial.println();

  Serial.print("Last RMT Completed Frame (RGB): ");
  for (uint8_t i = 0; i < pixelCount_; ++i) {
    Serial.printf("[%u]=(%u,%u,%u) ", i, lastConfirmedPixels_[i].r, lastConfirmedPixels_[i].g, lastConfirmedPixels_[i].b);
  }
  Serial.println();
}
#endif
