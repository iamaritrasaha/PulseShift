// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "config.h"
#include "led_effects.h"
#include "led_driver.h"

// ============================================================================
// LedController - sole owner and arbiter of the six WS2812B physical pixels.
//
// Separates frame calculation from physical driver transmission:
//   1. calculatedPixels_: Desired RGB output computed by LedEffectEngine.
//   2. lastAttemptedPixels_: Output submitted to driver on most recent write.
//   3. lastConfirmedPixels_: Output confirmed by hardware driver transmission.
//
// Manages transmission failure recovery (50 ms retry backoff) and periodic
// complete-frame refresh (1000 ms) to prevent silent freezing or ESD desync.
// ============================================================================
class LedController {
 public:
  static constexpr uint8_t LED_COUNT = config::ledCount;
  static constexpr uint32_t PERIODIC_REFRESH_INTERVAL_MS = 1000;
  static constexpr uint32_t RETRY_INTERVAL_MS = 50;
  static constexpr uint32_t INIT_RETRY_INTERVAL_MS = 1000;
  static constexpr uint8_t MAX_INIT_ATTEMPTS = 10;
  static constexpr uint32_t BENCH_TEST_DURATION_MS = 10000;

  LedController();
  ~LedController() = default;

  bool begin();
  // Render one frame and flush to hardware when output changed, retry is due, or periodic refresh is due.
  void update(const LiveRpmSnapshot& rpm, uint32_t now, bool healthy);

  void triggerButtonFeedback(uint32_t now) {
    feedbackStartMs_ = now;
    feedbackActive_ = true;
  }

  void setTestRpm(uint16_t rpm, bool active, uint32_t now) {
    testRpm_ = rpm;
    testRpmActive_ = active;
    testRpmStartMs_ = now;
  }
  bool isTestRpmActive() const { return testRpmActive_; }
  uint16_t testRpm() const { return testRpm_; }
  bool isLiveRpmRendering() const {
    const LedActiveLayer layer = engine_.activeLayer();
    return lastDriverStatus() == LedDriverStatus::TransmitOk &&
           (layer == LedActiveLayer::RpmBar || layer == LedActiveLayer::ProvisionalRpm ||
            layer == LedActiveLayer::Warning);
  }

  LedEffectEngine& ledEffects() { return engine_; }
  const LedEffectEngine& ledEffects() const { return engine_; }

  // Telemetry and state inspection
  bool isReady() const { return ready_; }
  bool isInitRecovering() const { return !ready_ && initAttempts_ > 0 && initAttempts_ < MAX_INIT_ATTEMPTS; }
  uint8_t initAttempts() const { return initAttempts_; }
  bool hasConfirmedFrame() const { return hasConfirmedFrame_; }
  uint32_t attemptsCount() const { return attemptsCount_; }
  uint32_t successCount() const { return successCount_; }
  uint32_t failureCount() const { return failureCount_; }
  uint32_t forcedRefreshCount() const { return forcedRefreshCount_; }
  uint32_t lastAttemptMs() const { return lastAttemptMs_; }
  uint32_t lastConfirmedMs() const { return lastConfirmedMs_; }
  LedDriverStatus lastDriverStatus() const { return driver_ ? driver_->lastStatus() : LedDriverStatus::Uninitialized; }
  const char* lastDriverStatusString() const { return driver_ ? driver_->lastStatusString() : "NO_DRIVER"; }

  RgbColor calculatedPixel(uint8_t i) const { return (i < LED_COUNT) ? calculatedPixels_[i] : RgbColor{0, 0, 0}; }
  RgbColor lastAttemptedPixel(uint8_t i) const { return (i < LED_COUNT) ? lastAttemptedPixels_[i] : RgbColor{0, 0, 0}; }
  RgbColor lastConfirmedPixel(uint8_t i) const { return (i < LED_COUNT) ? lastConfirmedPixels_[i] : RgbColor{0, 0, 0}; }

  // Test injection
  void setDriverForTesting(ILedDriver* driver) {
    driver_ = driver;
    if (driver_) {
      ready_ = driver_->isReady();
    } else {
      ready_ = false;
    }
    initAttempts_ = 0;
    lastInitAttemptMs_ = 0;
  }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  void printStatus(uint32_t now) const;
#endif

 private:
  void initializeDriver(uint32_t now);
  void transmitAttemptedFrame(uint32_t now);
  void recordError(uint32_t now);

#if defined(ARDUINO) && defined(ESP32)
  Esp32RmtLedDriver defaultDriver_{};
#else
  MockLedDriver defaultDriver_{};
#endif
  ILedDriver* driver_ = &defaultDriver_;
  LedEffectEngine engine_{};

  bool ready_ = false;
  uint8_t initAttempts_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  bool lastAttemptFailed_ = false;
  uint32_t feedbackStartMs_ = 0;
  bool feedbackActive_ = false;
  uint16_t testRpm_ = 0;
  bool testRpmActive_ = false;
  uint32_t testRpmStartMs_ = 0;

  // Tri-state buffer architecture
  RgbColor calculatedPixels_[LED_COUNT]{};
  RgbColor lastAttemptedPixels_[LED_COUNT]{};
  RgbColor lastConfirmedPixels_[LED_COUNT]{};

  bool hasConfirmedFrame_ = false;
  uint32_t attemptsCount_ = 0;
  uint32_t successCount_ = 0;
  uint32_t failureCount_ = 0;
  uint32_t forcedRefreshCount_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t lastConfirmedMs_ = 0;
  LedDriverStatus lastError_ = LedDriverStatus::Uninitialized;
  uint32_t lastErrorMs_ = 0;
  uint32_t lastErrorLoggedMs_ = 0;
};
