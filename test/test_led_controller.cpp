// Copyright (c) 2026 Aritra Saha. All rights reserved.
// Unit tests for LedController, tri-state buffer architecture, transmission
// retry on detectable failure, periodic complete-frame refresh, and mock driver verification.

#include "led_controller.h"
#include "config_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"LED_CTRL FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static unsigned checks = 0;

static PulseShiftConfig verifiedBarConfig() {
  PulseShiftConfig cfg{};
  cfg.isPowerOn = true;
  cfg.effect = 4; // Verified shift light
  cfg.calibrationVerified = true;
  cfg.calFingerprint = 0xDEADBEEF;
  cfg.calPointsUsed = 3;
  cfg.rpmThresholds[0] = 1000;
  cfg.rpmThresholds[1] = 2000;
  cfg.rpmThresholds[2] = 3000;
  cfg.rpmThresholds[3] = 4000;
  cfg.rpmThresholds[4] = 5000;
  cfg.rpmThresholds[5] = 6000;
  cfg.warningRpm = 7000;
  cfg.warningPatternId = 3; // Warning off
  cfg.warningColor = RgbColor{255, 255, 0};
  cfg.rpmHysteresis = 150;
  cfg.brightness = 255;
  for (uint8_t i = 0; i < 6; ++i) cfg.ledColors[i] = RgbColor{255, 0, 0};
  return cfg;
}

static void test_initial_black_flush() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);

  CHECK(ctrl.begin());
  CHECK(mock.isReady());
  CHECK(ctrl.hasConfirmedFrame());
  CHECK(ctrl.attemptsCount() == 1);
  CHECK(ctrl.successCount() == 1);
  CHECK(ctrl.failureCount() == 0);

  // All 6 pixels must be initialized to black
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((mock.lastWrittenPixel(i) == RgbColor{0, 0, 0}));
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }
}

static void test_bench_test_4500_to_0_rpm() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  ctrl.begin();

  ConfigManager::instance().activeMutable() = verifiedBarConfig();

  // Command 4,500 RPM bench test
  ctrl.setTestRpm(4500, true);
  ctrl.update(LiveRpmSnapshot{}, 100, true);

  CHECK(ctrl.isTestRpmActive());
  CHECK(ctrl.attemptsCount() == 2);
  CHECK(ctrl.successCount() == 2);
  CHECK(ctrl.ledEffects().activeLayer() == LedActiveLayer::BenchTest);

  // 4,500 RPM against thresholds 1000..6000 must light exactly 4 LEDs (0..3)
  for (uint8_t i = 0; i < 4; ++i) {
    CHECK((mock.lastWrittenPixel(i) == RgbColor{255, 0, 0}));
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{255, 0, 0}));
  }
  for (uint8_t i = 4; i < 6; ++i) {
    CHECK((mock.lastWrittenPixel(i) == RgbColor{0, 0, 0}));
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }

  // Transition to 0 RPM bench test
  ctrl.setTestRpm(0, true);
  ctrl.update(LiveRpmSnapshot{}, 150, true);

  CHECK(ctrl.attemptsCount() == 3);
  CHECK(ctrl.successCount() == 3);

  // All 6 LEDs must be commanded and confirmed black (OFF)
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((mock.lastWrittenPixel(i) == RgbColor{0, 0, 0}));
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }
}

static void test_retry_on_simulated_driver_failure() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  ctrl.begin();

  ConfigManager::instance().activeMutable() = verifiedBarConfig();

  // Simulate a hardware driver failure on the next write attempt
  mock.setFailNextWrites(1);

  ctrl.setTestRpm(3000, true);
  ctrl.update(LiveRpmSnapshot{}, 200, true);

  // Transmission was attempted but failed
  CHECK(ctrl.attemptsCount() == 2);
  CHECK(ctrl.failureCount() == 1);
  CHECK(ctrl.successCount() == 1); // Only the initial begin write succeeded
  CHECK(ctrl.lastDriverStatus() == LedDriverStatus::Timeout);

  // Crucial: lastConfirmedPixel remains old confirmed state (black), NOT the failed frame
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }

  // 10 ms later: retry backoff (50 ms) has NOT elapsed, no spam
  ctrl.update(LiveRpmSnapshot{}, 210, true);
  CHECK(ctrl.attemptsCount() == 2);

  // 50 ms later (t = 250): retry backoff expires, automatic retry fires!
  ctrl.update(LiveRpmSnapshot{}, 250, true);
  CHECK(ctrl.attemptsCount() == 3);
  CHECK(ctrl.successCount() == 2);
  CHECK(ctrl.lastDriverStatus() == LedDriverStatus::TransmitOk);

  // Frame is now confirmed with 3 LEDs lit (3000 RPM -> thresholds 1000, 2000, 3000)
  for (uint8_t i = 0; i < 3; ++i) {
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{255, 0, 0}));
  }
}

static void test_periodic_refresh() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  ctrl.begin();

  ConfigManager::instance().activeMutable() = verifiedBarConfig();
  ctrl.setTestRpm(1000, true);
  ctrl.update(LiveRpmSnapshot{}, 100, true);

  CHECK(ctrl.attemptsCount() == 2);
  CHECK(ctrl.forcedRefreshCount() == 0);

  // Steady state: intermediate updates at 200ms, 500ms, 900ms do not write redundant frames
  ctrl.update(LiveRpmSnapshot{}, 200, true);
  ctrl.update(LiveRpmSnapshot{}, 500, true);
  ctrl.update(LiveRpmSnapshot{}, 900, true);
  CHECK(ctrl.attemptsCount() == 2);
  CHECK(ctrl.forcedRefreshCount() == 0);

  // Periodic refresh after 1000 ms elapsed (t = 1101 ms)
  ctrl.update(LiveRpmSnapshot{}, 1101, true);
  CHECK(ctrl.attemptsCount() == 3);
  CHECK(ctrl.forcedRefreshCount() == 1);
  CHECK(ctrl.successCount() == 3);
}

static void test_master_power_off_and_restore() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  ctrl.begin();

  auto& cfg = ConfigManager::instance().activeMutable();
  cfg = verifiedBarConfig();
  ctrl.setTestRpm(4500, true);
  ctrl.update(LiveRpmSnapshot{}, 100, true);
  CHECK(ctrl.attemptsCount() == 2);

  // Turn master power OFF
  cfg.isPowerOn = false;
  ctrl.update(LiveRpmSnapshot{}, 200, true);

  CHECK(ctrl.ledEffects().activeLayer() == LedActiveLayer::Off);
  CHECK(ctrl.attemptsCount() == 3);
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }

  // Turn master power back ON
  cfg.isPowerOn = true;
  ctrl.update(LiveRpmSnapshot{}, 300, true);
  CHECK(ctrl.attemptsCount() == 4);
  CHECK(ctrl.ledEffects().activeLayer() == LedActiveLayer::BenchTest);
  for (uint8_t i = 0; i < 4; ++i) {
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{255, 0, 0}));
  }
}

int main() {
  test_initial_black_flush();
  test_bench_test_4500_to_0_rpm();
  test_retry_on_simulated_driver_failure();
  test_periodic_refresh();
  test_master_power_off_and_restore();

  std::printf("PASS LedController tri-state buffer, retry, refresh, mock driver: %u checks\n", checks);
  return 0;
}
