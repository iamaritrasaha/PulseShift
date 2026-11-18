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
  CHECK(e.activeLayer() == LedActiveLayer::Warning);
  CHECK(e.getPixel(0) == cfg.warningColor);

  input.canHealthy = false;
  e.update(10800, cfg, signal, input);
  CHECK(e.activeLayer() == LedActiveLayer::Fault);
}

static void preview_indicators_visible_without_can() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{};
  cfg.effect = 5;
  cfg.brightness = 255;
  cfg.startupAnimation = 1;
  cfg.startupDurationMs = 2000;
  LedFrameInput input;
  LiveRpmSnapshot noCan{};

  // No CAN candidate yet, but non-RPM boot indication must still be visible.
  e.update(100, cfg, noCan, input);
  CHECK(e.activeLayer() == LedActiveLayer::Startup);
  CHECK(!allBlack(e));

  // After the boot indication, invalid RPM must NOT render a phantom bar.
  e.update(2500, cfg, noCan, input);
  e.update(2600, cfg, noCan, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));

  // Owner enrollment is possible even without CAN traffic. Its cyan pulse
  // must not be masked by the preview's missing-signal safeguard.
  input.authWindowOpen = true;
  e.update(3250, cfg, noCan, input);
  CHECK(e.activeLayer() == LedActiveLayer::Commissioning);
  CHECK(e.getPixel(0).g > 0);
  input.authWindowOpen = false;
  e.update(3300, cfg, noCan, input);
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(e));

  // A stopped CAN controller still gets the higher priority red fault blink.
  input.canHealthy = false;
  e.update(3400, cfg, noCan, input);
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

static void verified_effect_bench_test_without_live_can() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  LedFrameInput input;
  input.testRpmActive = true;
  input.testRpm = 4500;
  LiveRpmSnapshot offline{};
  e.update(6000, cfg, offline, input);
  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 4);

  input.testRpmActive = false;
  e.update(6100, cfg, offline, input);
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

static void startup_animations_center_out_and_strobe() {
  // Animation 4: Center-Out staging tree (center pair first, then outward).
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg{};
  cfg.brightness = 255;
  cfg.startupAnimation = 4;
  cfg.startupDurationMs = 3000;
  cfg.ledColors[0] = RgbColor{10, 0, 0};
  cfg.ledColors[1] = RgbColor{0, 10, 0};
  cfg.ledColors[2] = RgbColor{0, 0, 10};
  cfg.ledColors[3] = RgbColor{0, 0, 10};
  cfg.ledColors[4] = RgbColor{0, 10, 0};
  cfg.ledColors[5] = RgbColor{10, 0, 0};
  LedFrameInput input;

  e.update(100, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::Startup);
  CHECK((e.getPixel(2) == RgbColor{0, 0, 10}));
  CHECK((e.getPixel(3) == RgbColor{0, 0, 10}));
  CHECK((e.getPixel(0) == RgbColor{0, 0, 0}));
  CHECK((e.getPixel(5) == RgbColor{0, 0, 0}));

  e.update(1500, cfg, LiveRpmSnapshot{}, input);
  CHECK((e.getPixel(1) == RgbColor{0, 10, 0}));
  CHECK((e.getPixel(4) == RgbColor{0, 10, 0}));
  CHECK((e.getPixel(0) == RgbColor{0, 0, 0}));

  // Animation 5: Strobe Rush flashes the whole strip, then ends solid.
  LedEffectEngine s;
  s.reset(0);
  PulseShiftConfig scfg{};
  scfg.brightness = 255;
  scfg.startupAnimation = 5;
  scfg.startupDurationMs = 3000;
  for (uint8_t i = 0; i < 6; ++i) scfg.ledColors[i] = RgbColor{50, 60, 70};

  s.update(100, scfg, LiveRpmSnapshot{}, input);
  CHECK(s.activeLayer() == LedActiveLayer::Startup);
  CHECK((s.getPixel(0) == RgbColor{50, 60, 70})); // first burst lit
  s.update(700, scfg, LiveRpmSnapshot{}, input);  // second 500 ms slot: dark
  CHECK((s.getPixel(0) == RgbColor{0, 0, 0}));
  s.update(2200, scfg, LiveRpmSnapshot{}, input); // >= 2/3 of duration: solid
  CHECK((s.getPixel(5) == RgbColor{50, 60, 70}));
  s.update(3200, scfg, LiveRpmSnapshot{}, input);
  CHECK(s.isStartupComplete());
  CHECK(s.activeLayer() == LedActiveLayer::Decorative);
  CHECK((s.getPixel(0) == RgbColor{50, 60, 70}));
}

static void boot_button_auth_window_does_not_obscure_active_rpm() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);

  // Test 1: Provisional RPM preview (effect 5)
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 5;
  cfg.calibrationVerified = false;
  cfg.calFingerprint = 0;
  cfg.calPointsUsed = 0;
  cfg.warningPatternId = 3;

  LedFrameInput input;
  LiveRpmSnapshot liveRpm = freshVerifiedSnapshot(4500); // 4 LEDs lit
  liveRpm.calibrationVerified = false;
  liveRpm.calibrationMatchesSettings = false;

  // Normal running state
  e.update(10000, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 4);

  // Button pressed while engine is running: brief white feedback flash
  input.feedbackActive = true;
  e.update(10050, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::Feedback);
  CHECK((e.getPixel(0) == RgbColor{255, 255, 255}));

  // Feedback expires (200ms), but auth window remains OPEN for 60 seconds.
  // The shift light MUST continue rendering 4500 RPM and NOT be masked by cyan!
  input.feedbackActive = false;
  input.authWindowOpen = true;
  e.update(10250, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 4);

  // Engine speed changes to 3000 RPM while auth window is open: shift light tracks it!
  liveRpm.rpm = 3000;
  e.update(10500, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 3);

  // Engine stops (e.g. at traffic light, 0 RPM):
  // Now cyan commissioning pulse appears because engine is not actively running.
  liveRpm.rpm = 0;
  liveRpm.engineRunning = false;
  e.update(11250, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::Commissioning);
  CHECK(e.getPixel(0).g > 0);

  // Engine starts up again and revs to 2500 RPM:
  // Instantly returns to shift light rendering!
  liveRpm.rpm = 2500;
  liveRpm.engineRunning = true;
  e.update(12000, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 2);

  // Auth window closes after 60s
  input.authWindowOpen = false;
  e.update(71000, cfg, liveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(e) == 2);

  // Test 2: Verified shift-light mode (effect 4)
  PulseShiftConfig vcfg = verifiedBarConfig();
  LiveRpmSnapshot vliveRpm = freshVerifiedSnapshot(3500);
  e.update(72000, vcfg, vliveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::RpmBar);
  CHECK(countLit(e) == 3);

  // Button pressed during verified mode with running engine
  input.authWindowOpen = true;
  e.update(72100, vcfg, vliveRpm, input);
  CHECK(e.activeLayer() == LedActiveLayer::RpmBar);
  CHECK(countLit(e) == 3);
}

static void bench_test_overrides_startup_and_commissioning_window() {
  LedEffectEngine e;
  e.reset(0);
  PulseShiftConfig cfg = verifiedBarConfig();
  LedFrameInput input;

  // Case 1: Startup animation running (t = 500 ms, duration = 2000 ms),
  // and owner commissioning window is OPEN (authWindowOpen = true).
  // Bench test at 4,500 RPM MUST take precedence over commissioning and startup!
  input.authWindowOpen = true;
  input.testRpmActive = true;
  input.testRpm = 4500;
  e.update(500, cfg, LiveRpmSnapshot{}, input);

  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 4);
  CHECK(e.isStartupComplete()); // Startup superseded

  // Case 2: 0 RPM test while auth window is open:
  input.testRpm = 0;
  e.update(600, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 0); // All 6 LEDs commanded OFF (black)

  // Case 3: Test expires after 10s:
  input.testRpmActive = false;
  input.testRpm = 0;
  e.update(10600, cfg, LiveRpmSnapshot{}, input);
  // Auth window is still open (60s window), so it transitions to commissioning pulse
  CHECK(e.activeLayer() == LedActiveLayer::Commissioning);

  // Auth window closes at t = 70000:
  input.authWindowOpen = false;
  e.update(70000, cfg, LiveRpmSnapshot{}, input);
  // Live engine not running on verified mode -> safeguard blank
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);

  // Engine runs at 3500 RPM:
  e.update(71000, cfg, freshVerifiedSnapshot(3500), input);
  CHECK(e.activeLayer() == LedActiveLayer::RpmBar);
  CHECK(countLit(e) == 3);
}

static void master_power_off_overrides_bench_test() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.isPowerOn = false; // Master power turned OFF
  LedFrameInput input;
  input.testRpmActive = true;
  input.testRpm = 4500;

  e.update(3000, cfg, LiveRpmSnapshot{}, input);
  // Master power switch off wins over bench test
  CHECK(e.activeLayer() == LedActiveLayer::Off);
  CHECK(countLit(e) == 0);
}

static void test_4500_to_0_rpm_transitions() {
  LedEffectEngine e;
  e.reset(0);
  completeStartup(e);
  PulseShiftConfig cfg = verifiedBarConfig();
  LedFrameInput input;
  input.testRpmActive = true;

  // 4500 RPM -> 4 LEDs
  input.testRpm = 4500;
  e.update(3000, cfg, LiveRpmSnapshot{}, input);
  CHECK(countLit(e) == 4);

  // Transition to 0 RPM -> 0 LEDs
  input.testRpm = 0;
  e.update(3100, cfg, LiveRpmSnapshot{}, input);
  CHECK(countLit(e) == 0);
  for (uint8_t i = 0; i < 6; ++i) {
    CHECK((e.getPixel(i) == RgbColor{0, 0, 0}));
  }

  // Transition back to 4500 RPM -> 4 LEDs
  input.testRpm = 4500;
  e.update(3200, cfg, LiveRpmSnapshot{}, input);
  CHECK(countLit(e) == 4);
}

static void test_zero_rpm_clears_low_thresholds_and_warning() {
  LedEffectEngine e;
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.rpmThresholds[0] = 0;
  cfg.rpmThresholds[1] = 0;
  cfg.rpmThresholds[2] = 50;
  cfg.rpmThresholds[3] = 60;
  cfg.rpmThresholds[4] = 70;
  cfg.rpmThresholds[5] = 80;
  cfg.warningRpm = 100;
  cfg.warningPatternId = 2;
  LedFrameInput input;
  input.authWindowOpen = true;
  input.testRpmActive = true;
  input.testRpm = 4500;
  e.update(500, cfg, LiveRpmSnapshot{}, input);
  CHECK(countLit(e) == 6);

  input.testRpm = 0;
  e.update(510, cfg, LiveRpmSnapshot{}, input);
  CHECK(e.activeLayer() == LedActiveLayer::BenchTest);
  CHECK(countLit(e) == 0);
}

static void test_bench_latches_do_not_contaminate_live_rpm() {
  LedEffectEngine e;
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.warningPatternId = 3;
  LedFrameInput input;
  input.testRpmActive = true;
  input.testRpm = 4500;
  e.update(500, cfg, LiveRpmSnapshot{}, input);
  CHECK(countLit(e) == 4);

  input.testRpmActive = false;
  e.update(510, cfg, freshVerifiedSnapshot(3900), input);
  CHECK(e.activeLayer() == LedActiveLayer::RpmBar);
  CHECK(countLit(e) == 3);
}

static void test_provisional_startup_ends_in_safeguard_with_invalid_rpm() {
  LedEffectEngine e;
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 5;
  cfg.startupAnimation = 1;
  cfg.startupDurationMs = 100;
  cfg.warningPatternId = 3;
  LiveRpmSnapshot invalidRpm = freshVerifiedSnapshot(4500);
  invalidRpm.valid = false;
  invalidRpm.engineRunning = false;
  e.reset(UINT32_MAX - 55);

  e.update(UINT32_MAX - 1, cfg, invalidRpm, LedFrameInput{});
  CHECK(e.activeLayer() == LedActiveLayer::Startup);
  e.update(44, cfg, invalidRpm, LedFrameInput{});
  CHECK(e.isStartupComplete());
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(countLit(e) == 0);
}

static void test_rainbow_phase_survives_long_uptime() {
  LedEffectEngine fresh;
  LedEffectEngine longRunning;
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 2;
  cfg.effectSpeed = 100;
  cfg.startupAnimation = 0;
  // Equal rainbow phase four periods past 32-bit multiplication overflow.
  const uint32_t uptime = 43000030;
  fresh.update(uptime % (40 * 256), cfg, LiveRpmSnapshot{}, LedFrameInput{});
  longRunning.update(uptime, cfg, LiveRpmSnapshot{}, LedFrameInput{});
  for (uint8_t i = 0; i < 6; ++i) CHECK(fresh.getPixel(i) == longRunning.getPixel(i));
}

static void test_zero_hysteresis_uses_exact_threshold() {
  LedEffectEngine e;
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.warningPatternId = 3;
  cfg.startupAnimation = 0;
  cfg.rpmHysteresis = 0;
  e.update(3000, cfg, freshVerifiedSnapshot(4000), LedFrameInput{});
  CHECK(countLit(e) == 4);
  e.update(3010, cfg, freshVerifiedSnapshot(3999), LedFrameInput{});
  CHECK(countLit(e) == 3);
}


static void optional_startup_loops_without_masking_rpm_or_safety() {
  LedEffectEngine engine;
  engine.reset(0);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 1; // Decorative: startup is allowed to loop continuously.
  cfg.startupAnimation = 2; // Wave
  cfg.startupDurationMs = static_cast<uint16_t>(0x8000u | 1000u);
  CHECK(ConfigManager::validateConfig(cfg));
  engine.update(200, cfg, LiveRpmSnapshot{}, LedFrameInput{});
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);
  const RgbColor phase1 = engine.getPixel(0);
  engine.update(1200, cfg, LiveRpmSnapshot{}, LedFrameInput{});
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);
  CHECK(engine.getPixel(0) == phase1);
  CHECK(!engine.isStartupComplete());

  LedFrameInput off{};
  cfg.isPowerOn = false;
  engine.update(1500, cfg, LiveRpmSnapshot{}, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Off);
  cfg.isPowerOn = true;

  off.canHealthy = false;
  engine.update(1600, cfg, LiveRpmSnapshot{}, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Fault);
  off.canHealthy = true;
  engine.update(1700, cfg, LiveRpmSnapshot{}, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);

  // Switching on Loop after the one-shot ends must restart it on that frame.
  cfg.startupDurationMs = 1000;
  engine.update(2800, cfg, LiveRpmSnapshot{}, off);
  engine.update(3900, cfg, LiveRpmSnapshot{}, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Decorative);
  cfg.startupDurationMs = static_cast<uint16_t>(0x8000u | 1000u);
  engine.update(4000, cfg, LiveRpmSnapshot{}, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);

  // On the motorcycle, live RPM must win while the engine is running.
  cfg.effect = 5;
  auto running = freshVerifiedSnapshot(4500);
  engine.update(4100, cfg, running, off);
  CHECK(engine.activeLayer() == LedActiveLayer::ProvisionalRpm);
  CHECK(countLit(engine) == 4);
  auto stopped = freshVerifiedSnapshot(0);
  engine.update(4200, cfg, stopped, off);
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);

  auto stale = stopped;
  stale.available = false;
  stale.fresh = false;
  engine.update(4300, cfg, stale, off);
  CHECK(engine.activeLayer() == LedActiveLayer::SafeguardBlank);
  CHECK(allBlack(engine));

  cfg.startupAnimation = 0;
  CHECK(!ConfigManager::validateConfig(cfg)); // Cannot loop a disabled sequence.
  cfg.startupDurationMs = 1000;
  CHECK(ConfigManager::validateConfig(cfg));
}

int main() {
  fault_priority_wins_over_everything();
  unverified_rpm_bar_renders_nothing();
  provisional_rpm_preview_tracks_throttle_without_calibration();
  provisional_default_keeps_synthetic_bench_test();
  verified_effect_bench_test_without_live_can();
  preview_indicators_visible_without_can();
  boot_button_auth_window_does_not_obscure_active_rpm();
  feedback_overrides_decorative_not_fault();
  startup_then_decorative();
  optional_startup_loops_without_masking_rpm_or_safety();
  startup_animations_center_out_and_strobe();
  rpm_bar_latches_and_hysteresis();
  warning_patterns();
  warning_hysteresis_no_chatter();
  direction_and_brightness_scaling();
  bench_test_preview_is_explicit();
  bench_test_overrides_startup_and_commissioning_window();
  master_power_off_overrides_bench_test();
  test_4500_to_0_rpm_transitions();
  test_zero_rpm_clears_low_thresholds_and_warning();
  test_bench_latches_do_not_contaminate_live_rpm();
  test_provisional_startup_ends_in_safeguard_with_invalid_rpm();
  test_rainbow_phase_survives_long_uptime();
  test_zero_hysteresis_uses_exact_threshold();
  master_power_off();
  change_detection_flags();
  std::printf("PASS unified LED engine priorities, safeguard, hysteresis, warnings: %u checks\n", checks);
  return 0;
}

// Pre-check trigger note: this suite is exercised first by the disposable precheck workflow.
