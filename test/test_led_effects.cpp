// Unified LED engine tests: layer priorities, RPM validity safeguard,
// per-LED hysteresis, warning patterns, direction and brightness scaling.
#include "led_effects.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

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
  CHECK(e.activeLayer() == LedActiveLayer::SafeguardBlank);
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


static void ambient_eligibility_and_priority() {
  LedEffectEngine engine;
  engine.reset(0);
  PulseShiftConfig cfg = verifiedBarConfig();
  cfg.effect = 5;
  cfg.ambient = {true, 2, 1000};
  LedFrameInput input{};
  engine.update(100, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::Startup);
  engine.update(2000, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::Ambient && engine.isStartupComplete());
  CHECK(engine.ambientStatus() == AmbientStatus::Playing);
  cfg.startupAnimation = 5;
  engine.update(5000, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::Ambient); // edits cannot restart boot

  input.canTrafficSeen = true;
  engine.update(5100, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::SafeguardBlank && allBlack(engine));
  CHECK(engine.ambientStatus() == AmbientStatus::SuppressedBySafety);
  engine.update(5200, cfg, freshVerifiedSnapshot(4500), input);
  CHECK(engine.activeLayer() == LedActiveLayer::ProvisionalRpm && countLit(engine) == 4);
  CHECK(engine.ambientStatus() == AmbientStatus::PausedByRpm);
  input.authWindowOpen = true;
  engine.update(5300, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::SafeguardBlank && allBlack(engine));
  input.authWindowOpen = false;
  engine.update(5400, cfg, freshVerifiedSnapshot(0), input);
  CHECK(engine.activeLayer() == LedActiveLayer::Ambient);

  cfg.isPowerOn = false;
  engine.update(5500, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::Off && allBlack(engine));
  input.canHealthy = false;
  engine.update(5600, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::Fault);
  input.canHealthy = cfg.isPowerOn = true;
  engine.update(5650, cfg, {}, input);
  CHECK(engine.activeLayer() == LedActiveLayer::SafeguardBlank);
  engine.update(5700, cfg, freshVerifiedSnapshot(0), input);
  CHECK(engine.activeLayer() == LedActiveLayer::Ambient);
}

static void all_ambient_patterns_keep_time() {
  for (uint8_t pattern = 1; pattern <= 5; ++pattern) {
    for (uint8_t pixels : {1, 6, 64}) {
      LedEffectEngine engine;
      const uint32_t start = UINT32_MAX - 517;
      engine.reset(start, pixels);
      PulseShiftConfig cfg = verifiedBarConfig();
      cfg.bootEnabled = false;
      cfg.installedLedCount = pixels;
      cfg.ambient = {true, pattern, 1000};
      RgbColor first[64]{};
      bool changes = false;
      engine.update(start, cfg, {}, {});
      for (uint8_t i = 0; i < pixels; ++i) first[i] = engine.getPixel(i);
      for (uint32_t delta = 50; delta <= 601000; delta += 50) {
        engine.update(start + delta, cfg, {}, {});
        CHECK(engine.activeLayer() == LedActiveLayer::Ambient);
        for (uint8_t i = 0; i < pixels; ++i) {
          if (delta % 1000 == 0) CHECK(first[i] == engine.getPixel(i));
          else changes |= first[i] != engine.getPixel(i);
        }
      }
      CHECK(changes);
    }
  }
}

static void dynamic_thresholds_and_inactive_pixels() {
  for (uint8_t count : {1, 2, 3, 6, 12, 24, 32, 64}) {
    PulseShiftConfig cfg = verifiedBarConfig();
    cfg.installedLedCount = count;
    auto& m = cfg.rpmMapping;
    m.mode = RpmMappingMode::Individual;
    m.activeLedCount = count;
    m.entries = count;
    for (uint8_t i = 0; i < count; ++i) m.thresholds[i] = 1000 + i * 50;
    CHECK(ConfigManager::validateConfig(cfg));
    LedEffectEngine engine;
    engine.reset(0, count);
    for (uint8_t step = 0; step < count; ++step) {
      engine.update(step * 50, cfg, freshVerifiedSnapshot(m.thresholds[step]), {});
      for (uint8_t pixel = 0; pixel < count; ++pixel) {
        CHECK((engine.getPixel(pixel) != RgbColor{}) == (pixel <= step));
      }
    }
    engine.update(4000, cfg, freshVerifiedSnapshot(m.thresholds[count - 1] - 149), {});
    CHECK(engine.getPixel(count - 1) != RgbColor{});
    engine.update(4010, cfg, freshVerifiedSnapshot(m.thresholds[count - 1] - 151), {});
    CHECK(engine.getPixel(count - 1) == RgbColor{});
  }
  auto cfg = verifiedBarConfig();
  cfg.installedLedCount = 24;
  cfg.ledDirection = 1;
  auto& m = cfg.rpmMapping;
  m.activeLedCount = 12;
  m.mode = RpmMappingMode::Grouped;
  m.entries = 6;
  const uint8_t sizes[] = {3, 3, 2, 2, 1, 1};
  for (uint8_t i = 0; i < 6; ++i) {
    m.groupSizes[i] = sizes[i];
    m.thresholds[i] = (i + 1) * 1000;
  }
  CHECK(ConfigManager::validateConfig(cfg));
  LedEffectEngine engine;
  engine.reset(0, 24);
  engine.update(0, cfg, freshVerifiedSnapshot(3000), {});
  for (uint8_t i = 0; i < 24; ++i) CHECK((engine.getPixel(i) != RgbColor{}) == (i >= 16));
  engine.update(30, cfg, freshVerifiedSnapshot(7000), {});
  CHECK(engine.activeLayer() == LedActiveLayer::Warning);
  for (uint8_t i = 0; i < 24; ++i) CHECK(engine.getPixel(i) == (i >= 12 ? cfg.warningColor : RgbColor{}));
  ++m.groupSizes[0];
  CHECK(!ConfigManager::validateConfig(cfg));

  m.mode = RpmMappingMode::Automatic;
  m.entries = 2;
  m.thresholds[0] = 2000;
  m.thresholds[1] = 8000;
  CHECK(!ConfigManager::validateConfig(cfg));
  cfg.warningRpm = 8001;
  CHECK(ConfigManager::validateConfig(cfg));
  uint16_t thresholds[64]{};
  m.expand(thresholds, 12, cfg.rpmThresholds);
  const uint16_t expected[] = {2000, 2545, 3091, 3636, 4182, 4727, 5273, 5818, 6364, 6909, 7455, 8000};
  CHECK(memcmp(thresholds, expected, sizeof(expected)) == 0);
  m.expand(thresholds, 1, cfg.rpmThresholds);
  CHECK(thresholds[0] == 2000);
}

static void effects_and_warning_scale_to_strip() {
  const uint8_t counts[] = {1, 2, 3, 6, 12, 24, 32, 64};
  for (uint8_t count : counts) {
    PulseShiftConfig cfg = verifiedBarConfig();
    cfg.installedLedCount = count;
    for (uint8_t zone = 0; zone < config::rpmStages; ++zone) {
      cfg.ledColors[zone] = RgbColor{static_cast<uint8_t>(50 + 20 * zone), 0, 0};
    }
    LedEffectEngine forward;
    LedEffectEngine reverse;
    forward.reset(0, count);
    reverse.reset(0, count);
    auto renderBoth = [&](uint32_t time, const LiveRpmSnapshot& rpm, LedActiveLayer layer,
                          LedFrameInput input = {}) {
      cfg.ledDirection = 0;
      forward.update(time, cfg, rpm, input);
      cfg.ledDirection = 1;
      reverse.update(time, cfg, rpm, input);
      CHECK(forward.activeLayer() == layer && reverse.activeLayer() == layer);
      for (uint8_t i = 0; i < count; ++i) CHECK(forward.getPixel(i) == reverse.getPixel(count - 1 - i));
      CHECK(forward.getPixel(count) == RgbColor{});
      CHECK(reverse.getPixel(255) == RgbColor{});
    };
    for (uint8_t animation = 1; animation <= 5; ++animation) {
      cfg.bootEnabled = false;
      cfg.ambient = {true, animation, 2000};
      cfg.effect = 0;
      for (uint32_t elapsed : {0u, 674u, 1300u, 1975u, 2674u}) {
        renderBoth(animation * 3000 + elapsed, {}, LedActiveLayer::Ambient);
      }
    }
    cfg.ambient.enabled = false;
    for (uint8_t effect = 0; effect <= 3; ++effect) {
      cfg.effect = effect;
      renderBoth(20000, {}, LedActiveLayer::Decorative);
    }
    cfg.effect = 4;
    renderBoth(22000, freshVerifiedSnapshot(3500), LedActiveLayer::RpmBar);
    for (uint8_t i = 0; i < count; ++i) {
      const uint8_t zone = i * config::rpmStages / count;
      CHECK(forward.getPixel(i) == (zone < 3 ? cfg.ledColors[zone] : RgbColor{}));
    }
    for (uint8_t warning = 0; warning <= 3; ++warning) {
      cfg.warningPatternId = warning;
      const uint32_t time = 23000 + warning * 1000;
      renderBoth(time, {}, LedActiveLayer::SafeguardBlank);
      renderBoth(time + 10, freshVerifiedSnapshot(7500),
                 warning == 3 ? LedActiveLayer::RpmBar : LedActiveLayer::Warning);
      for (uint8_t i = 0; i < count; ++i) {
        const uint8_t zone = i * config::rpmStages / count;
        CHECK(forward.getPixel(i) == (warning == 3 ? cfg.ledColors[zone] : cfg.warningColor));
      }
    }
    cfg.effect = 5;
    renderBoth(27000, freshVerifiedSnapshot(3600), LedActiveLayer::ProvisionalRpm);
    LedFrameInput failedCan;
    failedCan.canHealthy = false;
    renderBoth(28000, freshVerifiedSnapshot(7600), LedActiveLayer::Fault, failedCan);
  }
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
  ambient_eligibility_and_priority();
  all_ambient_patterns_keep_time();
  dynamic_thresholds_and_inactive_pixels();
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
  effects_and_warning_scale_to_strip();
  std::printf("PASS unified LED engine priorities, safeguard, hysteresis, warnings: %u checks\n", checks);
  return 0;
}

// Pre-check trigger note: this suite is exercised first by the disposable precheck workflow.
