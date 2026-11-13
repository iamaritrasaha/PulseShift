// Unified LED engine tests: layer priorities, RPM validity safeguard,
// per-LED hysteresis, warning patterns, direction and brightness scaling.
#include "led_effects.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"LED FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static unsigned checks=0;

static LiveRpmSnapshot freshVerifiedSnapshot(uint16_t rpm) {
  LiveRpmSnapshot s{};
  s.available = true;
  s.fresh = true;
  s.valid = true;
  s.engineRunning = rpm > 0;
  s.rpm = rpm;
  s.raw = static_cast<uint8_t>(rpm / 40);
  s.calibrationVerified = true;
  s.calibrationMatchesSettings = true;
  return s;
}

// Configured as a verified six-stage bar: thresholds 1000..6000, warning 7000.
static PulseShiftConfig verifiedBarConfig() {
  PulseShiftConfig cfg{};
  cfg.effect = 4;
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
  cfg.warningPatternId = 2; // Solid
  cfg.warningColor = RgbColor{255, 255, 0};
  cfg.rpmHysteresis = 150;
  cfg.brightness = 255;
  for (uint8_t i = 0; i < 6; ++i) cfg.ledColors[i] = RgbColor{255, 0, 0};
  return cfg;
}

// Drive one frame past the default 2000 ms startup window so later cases
// exercise the runtime layers, not the boot animation.
static void completeStartup(LedEffectEngine& e) {
  e.update(2000, PulseShiftConfig{}, LiveRpmSnapshot{}, LedFrameInput{});
}

static bool allBlack(const LedEffectEngine& e) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (e.getPixel(i) != RgbColor{0, 0, 0}) return false;
  }
  return true;
}

static uint8_t countLit(const LedEffectEngine& e) {
  uint8_t lit = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    if (e.getPixel(i) != RgbColor{0, 0, 0}) ++lit;
  }
  return lit;
}

static void fault_priority_wins_over_everything() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg = verifiedBarConfig();
  LedFrameInput input;
  input.canHealthy = false;
  input.feedbackActive = true;             // even feedback cannot override a fault
  input.authWindowOpen = true;
  input.testRpmActive = true;
  input.testRpm = 9000;
  e.update(400, cfg, freshVerifiedSnapshot(9000), input);
  CHECK(e.activeLayer() == LedActiveLayer::Fault);
  // Red blink phase A (on) at even 100 ms tick
  CHECK((e.getPixel(0) == RgbColor{255, 0, 0}));
  e.update(500, cfg, freshVerifiedSnapshot(9000), input);
  CHECK(allBlack(e)); // odd tick -> blink off
}

static void unverified_rpm_bar_renders_nothing() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.calibrationVerified = false; // no bound verification record
  cfg.calFingerprint = 0;
  cfg.calPointsUsed = 0;
  LedFrameInput input;
  e.update(1000, cfg, freshVerifiedSnapshot(5500), input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));

  // Same for a verified config fed an UNVERIFIED snapshot (mismatch).
  PulseShiftConfig cfg2 = verifiedBarConfig();
  LiveRpmSnapshot snap = freshVerifiedSnapshot(5500);
  snap.calibrationVerified = false;
  e.update(1001, cfg2, snap, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);

  // And for a STALE snapshot: the last RPM must never keep lighting LEDs.
  LiveRpmSnapshot stale = freshVerifiedSnapshot(5500);
  stale.fresh = false;
  stale.valid = false;
  stale.rpm = 0;
  e.update(1002, cfg2, stale, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));
}

// An explicitly provisional preview follows the real incoming CAN estimate
// without claiming independent calibration or imposing a throttle lock.
static void provisional_rpm_preview_tracks_throttle_without_calibration() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 5; // UNVERIFIED standalone diagnostic preview
  cfg.calibrationVerified = false;
  cfg.calFingerprint = 0;
  cfg.calPointsUsed = 0;
  cfg.warningPatternId = 3;
  LedFrameInput input;

  LiveRpmSnapshot signal = freshVerifiedSnapshot(900);
  signal.calibrationVerified = false;
  signal.calibrationMatchesSettings = false;
  e.update(10000, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 0);

  signal = freshVerifiedSnapshot(4500);
  signal.calibrationVerified = false;
  signal.calibrationMatchesSettings = false;
  e.update(10100, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 4);

  signal.fresh = false;
  signal.valid = false;
  signal.rpm = 0;
  e.update(10500, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));

  signal = freshVerifiedSnapshot(3000);
  signal.calibrationVerified = false;
  signal.calibrationMatchesSettings = false;
  e.update(10600, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 3); // stale signal reset previous latches

  cfg.warningPatternId = 2;
  signal = freshVerifiedSnapshot(7200);
  signal.calibrationVerified = false;
  signal.calibrationMatchesSettings = false;
  e.update(10700, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(e.getPixel(0) == cfg.warningColor);

  input.canHealthy = false;
  e.update(10800, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::Fault);
}

static void feedback_overrides_decorative_not_fault() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{}; // decorative static violet
  cfg.brightness = 255;   // unscaled: the engine dims output by cfg.brightness
  for (uint8_t i = 0; i < 6; ++i) cfg.ledColors[i] = RgbColor{140, 43, 238};
  LedFrameInput input;
  input.feedbackActive = true;
  e.update(2000, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::Feedback);
  CHECK((e.getPixel(0) == RgbColor{255, 255, 255}));
}

static void startup_then_decorative() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{};
  cfg.brightness = 255;
  cfg.startupAnimation = 1;
  cfg.startupDurationMs = 2000;
  for (uint8_t i = 0; i < 6; ++i) cfg.ledColors[i] = RgbColor{10, 20, 30};
  LedFrameInput input;
  e.update(100, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::Startup);
  CHECK(!e.isStartupComplete());
  e.update(2500, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.isStartupComplete());
  CHECK(e.activeLayer() == LedActiveLayer::Decorative);
  CHECK((e.getPixel(0) == RgbColor{10, 20, 30}));
}

static void rpm_bar_latches_and_hysteresis() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.warningPatternId = 3; // warning off for this case
  LedFrameInput input;

  // Below first threshold: all dark.
  e.update(10000, cfg, freshVerifiedSnapshot(900), input);
  CHECK(e.activeLayer() == LedActiveLayer::RpmBar);
  CHECK(countLit(e) == 0);

  // Rising through 2500 lights LEDs 0 and 1 (1000, 2000 thresholds).
  e.update(10100, cfg, freshVerifiedSnapshot(2500), input);
  CHECK(countLit(e) == 2);

  // Falling to 1950: LED 1 stays lit (hysteresis 150 -> off only below 1850).
  e.update(10200, cfg, freshVerifiedSnapshot(1950), input);
  CHECK(countLit(e) == 2);
  // Falling below 1850 clears it.
  e.update(10300, cfg, freshVerifiedSnapshot(1800), input);
  CHECK(countLit(e) == 1);

  // All six above 6000.
  e.update(10400, cfg, freshVerifiedSnapshot(6500), input);
  CHECK(countLit(e) == 6);
}

static void warning_patterns() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  LedFrameInput input;

  // Solid warning above 7000: every pixel is the warning color.
  e.update(20000, cfg, freshVerifiedSnapshot(7500), input);
  for (uint8_t i = 0; i < 6; ++i) CHECK(e.getPixel(i) == cfg.warningColor);

  // Flash pattern toggles with the 100 ms period (latched at t=20000).
  cfg.warningPatternId = 0;
  e.update(20100, cfg, freshVerifiedSnapshot(7500), input);
  CHECK((e.getPixel(0) == RgbColor{0, 0, 0})); // (20100-20000)/100 = 1 -> off phase
  e.update(20250, cfg, freshVerifiedSnapshot(7500), input);
  CHECK(e.getPixel(0) == cfg.warningColor); // (250)/100 = 2 -> on phase

  // Warning OFF (pattern 3) keeps the bar.
  cfg.warningPatternId = 3;
  e.update(20300, cfg, freshVerifiedSnapshot(7500), input);
  CHECK(countLit(e) == 6);
}

static void warning_hysteresis_no_chatter() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig(); // warning 7000, hysteresis 150
  LedFrameInput input;
  CHECK(cfg.warningPatternId == 2);

  e.update(30000, cfg, freshVerifiedSnapshot(7100), input);
  CHECK(e.getPixel(0) == cfg.warningColor);
  // Between 6850 and 7000: warning stays latched (no chatter).
  e.update(30100, cfg, freshVerifiedSnapshot(6900), input);
  CHECK(e.getPixel(0) == cfg.warningColor);
  // Below 6850 it clears.
  e.update(30200, cfg, freshVerifiedSnapshot(6800), input);
  CHECK((e.getPixel(0) == RgbColor{255, 0, 0})); // bar colors, not warning color
}

static void direction_and_brightness_scaling() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg{};
  cfg.brightness = 128;
  cfg.ledDirection = 1; // Reversed
  cfg.ledColors[0] = RgbColor{255, 0, 0};
  cfg.ledColors[5] = RgbColor{0, 0, 255};
  cfg.effect = 0;
  LedFrameInput input;
  e.update(100, cfg, LiveRpmSnapshot{}, input);
  // Physical pixel 0 shows logical LED 5, scaled to ~50%.
  CHECK((e.getPixel(0) == RgbColor{0, 0, 128}));
  CHECK((e.getPixel(5) == RgbColor{128, 0, 0}));
}

static void bench_test_preview_is_explicit() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  // Decorative effect selected, but the bench-test preview renders the six
  // stage bar against the saved thresholds (1000..6000 from the bar config).
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 0; // decorative, NOT the RPM bar
  LedFrameInput input;
  input.testRpmActive = true;
  input.testRpm = 4500;
  e.update(500, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 4); // thresholds 1000..6000 -> 4500 lights 0..3
}

static void provisional_default_keeps_synthetic_bench_test() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 5;
  cfg.calibrationVerified = false;
  cfg.calFingerprint = 0;
  cfg.calPointsUsed = 0;
  LedFrameInput input;
  input.testRpmActive = true;
  input.testRpm = 4500;
  // No CAN sample, but a user-issued bench test still works.
  e.update(6000, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 4);
  input.testRpmActive = false;
  e.update(6100, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));
}

static void master_power_off() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{};
  cfg.isPowerOn = false;
  LedFrameInput input;
  e.update(100, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::Off);
  CHECK(allBlack(e));
}

// Change-detection contract: update() returns true exactly when the visible
// output changed, so the controller issues a physical leds_.show() only for
// real transitions (including lit -> all-black) and skips redundant writes.
static void change_detection_flags() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{};
  cfg.brightness = 255;
  cfg.effect = 0; // static
  for (uint8_t i = 0; i < 6; ++i) cfg.ledColors[i] = RgbColor{200, 0, 0};
  LedFrameInput input;

  // First rendered frame differs from the (black) initial state: changed.
  CHECK(e.update(3000, cfg, LiveRpmSnapshot{}, input));
  // Identical static frame: unchanged, no physical write required.
  CHECK(!e.update(3100, cfg, LiveRpmSnapshot{}, input));
  CHECK(!e.update(3300, cfg, LiveRpmSnapshot{}, input));

  // Lit -> all-black (power off) must report changed so the LEDs actually
  // turn off on the strip.
  PulseShiftConfig off = cfg;
  off.isPowerOn = false;
  CHECK(e.update(3400, off, LiveRpmSnapshot{}, input));
  CHECK(allBlack(e));
  // Staying off: unchanged.
  CHECK(!e.update(3500, off, LiveRpmSnapshot{}, input));

  // Black -> lit again: changed.
  CHECK(e.update(3600, cfg, LiveRpmSnapshot{}, input));

  // Color change on one LED: changed; then stable: unchanged.
  PulseShiftConfig cfg2 = cfg;
  cfg2.ledColors[3] = RgbColor{0, 200, 0};
  CHECK(e.update(3700, cfg2, LiveRpmSnapshot{}, input));
  CHECK(!e.update(3800, cfg2, LiveRpmSnapshot{}, input));

  // Direction flip changes the physical mapping: changed.
  PulseShiftConfig rev = cfg2;
  rev.ledDirection = 1;
  CHECK(e.update(3900, rev, LiveRpmSnapshot{}, input));
  CHECK(!e.update(4000, rev, LiveRpmSnapshot{}, input));
}

int main() {
  fault_priority_wins_over_everything();
  unverified_rpm_bar_renders_nothing();
  provisional_rpm_preview_tracks_throttle_without_calibration();
  provisional_default_keeps_synthetic_bench_test();
  feedback_overrides_decorative_not_fault();
  startup_then_decorative();
  rpm_bar_latches_and_hysteresis();
  warning_patterns();
  warning_hysteresis_no_chatter();
  direction_and_brightness_scaling();
  bench_test_preview_is_explicit();
  master_power_off();
  change_detection_flags();
  std::printf("PASS unified LED engine priorities, safeguard, hysteresis, warnings: %u checks\n", checks);
  return 0;
}

// Pre-check trigger note: this suite is exercised first by the disposable precheck workflow.
