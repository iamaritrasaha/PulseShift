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
  ctrl.setTestRpm(4500, true, 100);
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
  ctrl.setTestRpm(0, true, 150);
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

  ctrl.setTestRpm(3000, true, 200);
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
  ctrl.setTestRpm(1000, true, 100);
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
  ctrl.setTestRpm(4500, true, 100);
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

static void test_initialization_recovery() {
  MockLedDriver mock;
  mock.setFailBegin(1); // Fail initial begin() call

  LedController ctrl;
  ctrl.setDriverForTesting(&mock);

  CHECK(!ctrl.begin());
  CHECK(!ctrl.isReady());
  CHECK(ctrl.lastDriverStatus() == LedDriverStatus::InitFailed);
  CHECK(ctrl.initAttempts() == 1);
  CHECK(ctrl.isInitRecovering());

  // Intermediate updates before INIT_RETRY_INTERVAL_MS (1000 ms) do not spam re-initialization
  ctrl.update(LiveRpmSnapshot{}, 200, true);
  ctrl.update(LiveRpmSnapshot{}, 500, true);
  ctrl.update(LiveRpmSnapshot{}, 900, true);
  CHECK(!ctrl.isReady());
  CHECK(ctrl.initAttempts() == 1);

  // At t = 1000 ms, bounded nonblocking recovery fires and succeeds
  ctrl.update(LiveRpmSnapshot{}, 1000, true);
  CHECK(ctrl.isReady());
  CHECK(ctrl.initAttempts() == 2);
  CHECK(ctrl.hasConfirmedFrame());
  CHECK(ctrl.successCount() == 1);
  CHECK(ctrl.lastDriverStatus() == LedDriverStatus::TransmitOk);

  // Verify that all 6 pixels are flushed to black upon recovery
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((mock.lastWrittenPixel(i) == RgbColor{0, 0, 0}));
    CHECK((ctrl.lastConfirmedPixel(i) == RgbColor{0, 0, 0}));
  }
}

class RejectingDriver : public MockLedDriver {
 public:
  bool begin(uint8_t pin, uint8_t count) override {
    channelLost = false;
    return MockLedDriver::begin(pin, count);
  }
  bool write(const RgbColor* pixels, uint8_t count) override {
    if (channelLost) return false;
    return MockLedDriver::write(pixels, count);
  }
  bool isReady() const override { return !channelLost && MockLedDriver::isReady(); }
  LedDriverStatus lastStatus() const override {
    return channelLost ? LedDriverStatus::TransmitRejected : MockLedDriver::lastStatus();
  }
  bool channelLost = false;
};

static void test_runtime_channel_loss_and_partial_reinit() {
  RejectingDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  CHECK(ctrl.begin());
  auto cfg = verifiedBarConfig();
  ConfigManager::instance().activeMutable() = cfg;
  ctrl.setTestRpm(4500, true, 100);
  ctrl.update(LiveRpmSnapshot{}, 100, true);
  CHECK(ctrl.lastConfirmedPixel(3) == cfg.ledColors[3]);

  mock.channelLost = true;
  ctrl.setTestRpm(0, true, 150);
  ctrl.update(LiveRpmSnapshot{}, 150, true);
  CHECK(!ctrl.isReady());
  CHECK(ctrl.isInitRecovering());
  CHECK(ctrl.lastConfirmedPixel(3) == cfg.ledColors[3]);
  CHECK(ctrl.lastAttemptedPixel(3) == RgbColor{});
  CHECK(ctrl.calculatedPixel(3) == RgbColor{});
  ctrl.update(LiveRpmSnapshot{}, 1149, true);
  CHECK(ctrl.initAttempts() == 1);

  // Reacquire RMT, then fail its initial clear once. The historical completed
  // frame remains intact, and the normal 50 ms write backoff applies.
  mock.setFailNextWrites(1);
  ctrl.update(LiveRpmSnapshot{}, 1150, true);
  CHECK(ctrl.isReady());
  CHECK(ctrl.initAttempts() == 2);
  CHECK(ctrl.lastConfirmedPixel(3) == cfg.ledColors[3]);
  ctrl.update(LiveRpmSnapshot{}, 1199, true);
  CHECK(ctrl.lastConfirmedPixel(3) == cfg.ledColors[3]);
  ctrl.update(LiveRpmSnapshot{}, 1200, true);
  CHECK(ctrl.lastConfirmedPixel(3) == RgbColor{});
  CHECK(ctrl.attemptsCount() == 5);
  CHECK(ctrl.failureCount() == 2);
}

static void test_initialization_exhausts_ten_attempts() {
  MockLedDriver mock;
  mock.setFailBegin(20);
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  CHECK(!ctrl.begin());
  for (uint32_t now = 100; now <= 16000; now += 100) {
    ctrl.update(LiveRpmSnapshot{}, now, true);
  }
  CHECK(ctrl.initAttempts() == LedController::MAX_INIT_ATTEMPTS);
  CHECK(!ctrl.isInitRecovering());
  CHECK(!ctrl.isReady());
  CHECK(ctrl.attemptsCount() == 0);
}

static void test_bench_and_refresh_across_uptime_wrap() {
  MockLedDriver mock;
  LedController ctrl;
  ctrl.setDriverForTesting(&mock);
  CHECK(ctrl.begin());
  auto cfg = verifiedBarConfig();
  cfg.startupAnimation = 1;
  cfg.startupDurationMs = 2000;
  auto& config = ConfigManager::instance();
  config.activeMutable() = cfg;

  // Use the same tick timestamp for issuing and rendering the command while
  // commissioning is active. The test and its refresh both span millis wrap.
  const uint32_t start = UINT32_MAX - 500;
  config.triggerAuthWindow(start - 1);
  ctrl.setTestRpm(4500, true, start);
  ctrl.update(LiveRpmSnapshot{}, start, true);
  CHECK(ctrl.ledEffects().activeLayer() == LedActiveLayer::BenchTest);
  CHECK(ctrl.lastConfirmedPixel(3) == cfg.ledColors[3]);
  const auto before = ctrl.forcedRefreshCount();
  ctrl.update(LiveRpmSnapshot{}, start + 999, true);
  CHECK(ctrl.forcedRefreshCount() == before);
  ctrl.update(LiveRpmSnapshot{}, start + 1000, true);
  CHECK(ctrl.forcedRefreshCount() == before + 1);

  ctrl.update(LiveRpmSnapshot{}, start + LedController::BENCH_TEST_DURATION_MS - 1, true);
  CHECK(ctrl.isTestRpmActive());
  ctrl.update(LiveRpmSnapshot{}, start + LedController::BENCH_TEST_DURATION_MS, true);
  CHECK(!ctrl.isTestRpmActive());
  CHECK(ctrl.ledEffects().activeLayer() == LedActiveLayer::Commissioning);
}

int main() {
  test_initial_black_flush();
  test_bench_test_4500_to_0_rpm();
  test_retry_on_simulated_driver_failure();
  test_periodic_refresh();
  test_master_power_off_and_restore();
  test_initialization_recovery();
  test_runtime_channel_loss_and_partial_reinit();
  test_initialization_exhausts_ten_attempts();
  test_bench_and_refresh_across_uptime_wrap();

  std::printf("PASS LedController tri-state buffer, retry, refresh, mock driver, init recovery: %u checks\n", checks);
  return 0;
}
