// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "rpm_decoder.h"
#include "dashboard_model.h"
#include "tx_tracker.h"
#include "simulated_ecu.h"
#include "shift_light.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>

namespace {
unsigned assertions = 0, cases = 0;
#define CHECK(expr) do { ++assertions; if (!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); std::exit(1); } } while(0)
void pass(const char* name) { ++cases; std::printf("PASS %s\n",name); }

CanFrame frame(uint16_t raw=2430) {
  auto f=demonstrationFrame(100,0); f.data[0]=uint8_t(raw); f.data[1]=uint8_t(raw>>8); return f;
}

void decoding() {
  auto m=demonstrationMapping(); auto f=frame(); uint16_t rpm=42;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  pass("valid LE CAN frame");
  f.id=0x301; CHECK(decodeRpm(f,m,rpm)==DecodeResult::WrongId);
  f.id=0x800; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Malformed);
  f=frame(); f.extended=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::WrongId);
  m.extended=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid);
  f.id=m.id=0x1FFFFFFF; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid);
  f.id=0x20000000; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Malformed);
  pass("identifier width and standard/extended distinction");
  m=demonstrationMapping(); f=frame();
  for(unsigned dlc=0;dlc<256;++dlc) {
    f.dlc=uint8_t(dlc); rpm=42;
    const auto r=decodeRpm(f,m,rpm);
    CHECK(r==(dlc==8 ? DecodeResult::Valid : dlc>8 ? DecodeResult::Malformed : DecodeResult::WrongDlc));
    CHECK(rpm==(dlc==8 ? 2430 : 42));
  }
  pass("all DLC values and output preservation on rejection");
  f=frame(); f.remote=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Remote);
  m.enabled=false; f.remote=false; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Disabled);
  pass("RTR rejection and disabled bike mapping");
  m=demonstrationMapping(); m.littleEndian=false; f=frame(); f.data[0]=0x09; f.data[1]=0x7E;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  m.offset=6; f.data[6]=0x09; f.data[7]=0x7E;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  m.offset=7; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m.offset=255; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m.offset=0; m.denominator=0; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m=demonstrationMapping(); m.id=0x800; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  pass("BE, offsets, mapping bounds, zero divisor");
  m=demonstrationMapping(); m.numerator=3; m.denominator=2;
  f=frame(3); CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==4);
  f=frame(4334); CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange);
  m.numerator=1; m.denominator=2; f=frame(13001);
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange); // 6500.5 must not round into range
  m.numerator=0xFFFFFFFF; m.denominator=0xFFFFFFFF; f=frame(6500);
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==6500);
  m.denominator=1; CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange);
  pass("rational conversion, rounding and overflow safety");
  m=demonstrationMapping();
  for(unsigned raw=0;raw<=65535;++raw) {
    f=frame(uint16_t(raw)); rpm=42; const auto result=decodeRpm(f,m,rpm);
    CHECK(result==(raw<=6500 ? DecodeResult::Valid : DecodeResult::OutOfRange));
    CHECK(rpm==(raw<=6500 ? raw : 42));
  }
  pass("exhaustive 16-bit RPM range including zero and limits");
}

void envelope() {
  CanFrame f{}; f.dlc=8; f.extended=false;
  f.id=0x7FF; CHECK(validEnvelope(f));
  f.id=0x800; CHECK(!validEnvelope(f));
  f.extended=true;
  f.id=0x1FFFFFFF; CHECK(validEnvelope(f));
  f.id=0x20000000; CHECK(!validEnvelope(f));
  f.id=0x100; f.extended=false;
  f.dlc=8; CHECK(validEnvelope(f));
  f.dlc=9; CHECK(!validEnvelope(f));
  f.dlc=0; CHECK(validEnvelope(f));
  pass("validEnvelope: ID and DLC boundary conditions");
}

void decode_names() {
  const DecodeResult all[] = {
    DecodeResult::Valid, DecodeResult::Disabled, DecodeResult::Malformed,
    DecodeResult::Remote, DecodeResult::WrongId, DecodeResult::WrongDlc,
    DecodeResult::BadMapping, DecodeResult::OutOfRange
  };
  for(auto r : all) {
    const char* n = decodeName(r);
    CHECK(n != nullptr);
    CHECK(n[0] != '\0');
  }
  pass("decodeName: all enum values return non-empty strings");
}

void state() {
  RpmState s(demonstrationMapping());
  CHECK(!s.fresh(0,1000)); CHECK(!s.hasValue());
  CHECK(viewStatus(s,0,1000,true)==ViewStatus::Disconnected);
  auto f=frame(0); f.timestampMs=0; CHECK(s.ingest(f)==DecodeResult::Valid);
  CHECK(s.fresh(999,1000)); CHECK(s.rpm()==0); CHECK(!s.fresh(1000,1000));
  CHECK(viewStatus(s,1000,1000,true)==ViewStatus::Stale);
  f=frame(6501); f.timestampMs=1100; s.ingest(f);
  CHECK(!s.fresh(1100,1000)); CHECK(s.rpm()==0); CHECK(s.busFresh(1100,1000));
  f=frame(4000); f.timestampMs=1200; s.ingest(f);
  CHECK(s.fresh(1200,1000)); CHECK(s.rpm()==4000); CHECK(s.accepted()==2); CHECK(s.rejected()==1);
  CHECK(viewStatus(s,1200,1000,false)==ViewStatus::Fault);
  CHECK(viewStatus(s,1200,1000,true,false)==ViewStatus::Unconfigured);
  pass("missing, valid zero, stale boundary, invalid non-refresh, recovery and faults");
  RpmState wrap(demonstrationMapping()); f.timestampMs=0xFFFFFFF0; wrap.ingest(f);
  CHECK(wrap.fresh(0x20,1000)); CHECK(!wrap.fresh(0x400,1000));
  pass("millis rollover");
  RpmState bike(RpmMapping{}); f.timestampMs=20; bike.ingest(f);
  CHECK(!bike.hasValue()); CHECK(viewStatus(bike,20,1000,true)==ViewStatus::Unmapped);
  CHECK(viewStatus(bike,1020,1000,true)==ViewStatus::Disconnected);
  RpmState invalid(demonstrationMapping()); f.dlc=1; invalid.ingest(f);
  CHECK(viewStatus(invalid,20,1000,true)==ViewStatus::Invalid);
  pass("unmapped, invalid and disconnected states");
  RpmState ctr(demonstrationMapping());
  CHECK(ctr.accepted()==0); CHECK(ctr.rejected()==0);
  auto g=frame(100); g.timestampMs=0; ctr.ingest(g);
  CHECK(ctr.accepted()==1); CHECK(ctr.rejected()==0);
  g=frame(9999); g.timestampMs=0; ctr.ingest(g);
  CHECK(ctr.accepted()==1); CHECK(ctr.rejected()==1);
  g.remote=true; g.dlc=8; ctr.ingest(g);
  CHECK(ctr.accepted()==1); CHECK(ctr.rejected()==2);
  pass("RpmState accepted/rejected counters");
}

void leds() {
  CHECK(litLedCount(0,6500,6,true)==0);
  for(unsigned n=1;n<=6;++n) {
    unsigned threshold=(6500*n+5)/6;
    CHECK(litLedCount(uint16_t(threshold-1),6500,6,true)==uint8_t(n-1));
    CHECK(litLedCount(uint16_t(threshold),6500,6,true)==uint8_t(n));
  }
  CHECK(litLedCount(65535,6500,6,true)==6);
  CHECK(litLedCount(6500,6500,6,false)==0);
  CHECK(litLedCount(6500,0,6,true)==0);
  CHECK(litLedCount(6500,6500,0,true)==0);
  CHECK(litLedCount(6499,6500,1,true)==0);
  CHECK(litLedCount(6500,6500,1,true)==1);
  pass("six LED thresholds, clamping, zero count and stale/off behavior");
}

void tx() {
  auto f=frame(); TxTracker t;
  CHECK(t.begin(f,0)); CHECK(!t.begin(f,0)); CHECK(t.onReceive(f)); CHECK(!t.finish(1)); CHECK(!t.finish(0));
  CHECK(!t.timedOut(1499,1500)); CHECK(t.timedOut(1500,1500)); CHECK(t.halt());
  CHECK(!t.halt()); CHECK(!t.timedOut(1600,1500)); CHECK(!t.begin(f,2000)); CHECK(!t.onTxSuccess());
  pass("original RX-without-TX regression, single timeout, no queue flooding");
  for(unsigned order=0;order<2;++order) {
    TxTracker ok; CHECK(ok.begin(f,0));
    if(order==0) { CHECK(ok.onReceive(f)); CHECK(ok.onTxSuccess()); }
    else { CHECK(ok.onTxSuccess()); CHECK(ok.onReceive(f)); }
    CHECK(!ok.finish(1)); CHECK(ok.finish(0)); CHECK(!ok.pending()); CHECK(ok.begin(f,100));
  }
  TxTracker mismatch; CHECK(mismatch.begin(f,0)); f.data[4]^=1;
  CHECK(!mismatch.onReceive(f)); f.data[4]^=1;
  CHECK(mismatch.onReceive(f)); CHECK(!mismatch.onReceive(f));
  pass("RX/TX ordering, driver pending requirement, sequence mismatch and duplicates");
  {
    TxTracker rtr_test; auto fr=frame(); CHECK(rtr_test.begin(fr,0));
    auto fr_rtr=fr; fr_rtr.remote=true;
    CHECK(!rtr_test.onReceive(fr_rtr));
    pass("TxTracker rejects RTR frames");
  }
  {
    TxTracker dlc_test; auto fd=frame(); CHECK(dlc_test.begin(fd,0));
    auto fd_bad=fd; fd_bad.dlc=7;
    CHECK(!dlc_test.onReceive(fd_bad));
    pass("TxTracker rejects DLC-mismatched frames");
  }
  {
    TxTracker ext_test; auto fe=frame(); CHECK(ext_test.begin(fe,0));
    auto fe_ext=fe; fe_ext.extended=true;
    CHECK(!ext_test.onReceive(fe_ext));
    pass("TxTracker rejects extended-flag-mismatched frames");
  }
}

void replay() {
  ReplayTransport replay; RpmState s(demonstrationMapping()); CanFrame f;
  for(uint32_t now=0;now<=20000;now+=100) {
    const bool got=replay.receive(now,f);
    if(got) s.ingest(f);
    if(now==100) CHECK(s.fresh(now,1000));
    if(now==13500) CHECK(!s.fresh(now,1000));
    if(now==16000) { CHECK(!got); CHECK(!s.busFresh(now,1000)); }
    if(now==17000) CHECK(s.fresh(now,1000));
  }
  pass("full-frame replay, invalid interval, silence and resumed frames");
  {
    ReplayTransport seq_replay; CanFrame sf;
    bool got0 = seq_replay.receive(100, sf);
    CHECK(got0);
    uint32_t seq0 = uint32_t(sf.data[2]) | (uint32_t(sf.data[3])<<8)
                  | (uint32_t(sf.data[4])<<16) | (uint32_t(sf.data[5])<<24);
    bool got1 = seq_replay.receive(200, sf);
    CHECK(got1);
    uint32_t seq1 = uint32_t(sf.data[2]) | (uint32_t(sf.data[3])<<8)
                  | (uint32_t(sf.data[4])<<16) | (uint32_t(sf.data[5])<<24);
    CHECK(seq1 == seq0 + 1);
    pass("demonstration frame sequence counter increments by 1");
  }
}

// ---------------------------------------------------------------------------
// Shift-light engine test suite
// ---------------------------------------------------------------------------

ShiftLightConfig defaultTestConfig() {
  ShiftLightConfig c;
  c.ledCount = 6;
  c.masterBrightness = 255;  // 100% brightness for unambiguous RGB checks
  c.style = ShiftLightStyle::Classic;
  c.direction = LedDirection::Normal;
  c.startupAnimationEnabled = false;  // test direct live rendering first
  c.smoothTransitions = true;
  c.alertMode = ShiftAlertMode::FlashRed;
  c.rpmStart = 1500;
  c.rpmShift = 5800;
  c.rpmMax = 6500;
  c.hysteresisRpm = 150;
  c.animationIntervalMs = 0;  // immediate updates in unit tests
  c.idleEffectEnabled = false;
  c.idleMaxRpm = 1300;
  return c;
}

void shift_light_progressive_fill() {
  auto cfg = defaultTestConfig();
  ShiftLightEngine engine(cfg);
  engine.reset(0);

  // 1. Below start RPM (1500): all LEDs off
  engine.update(100, 0, ViewStatus::Live);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  engine.update(150, 1499, ViewStatus::Live);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // Exactly at start RPM: progress = 0, all off
  engine.update(200, 1500, ViewStatus::Live);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 2. Progressive progression: span = 5800 - 1500 = 4300 RPM.
  // Each LED span = 4300 / 6 = 716.67 RPM.
  // At 1500 + 717 = 2217 RPM: LED 0 is 100% lit (Green), LED 1 is off or starting.
  engine.update(250, 2217, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 0));
  CHECK(engine.getPixel(2) == RgbColor(0, 0, 0));
  CHECK(engine.getPixel(5) == RgbColor(0, 0, 0));

  // 3. Smooth transition test:
  // Mid-way through LED 1 (at ~2575 RPM: 1.5 LEDs).
  // LED 0 is 100% Green; LED 1 is partial Green (~50% brightness); LEDs 2..5 off.
  engine.update(300, 2575, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 0));
  RgbColor p1 = engine.getPixel(1);
  CHECK(p1.g > 60 && p1.g < 200 && p1.r == 0 && p1.b == 0);
  CHECK(engine.getPixel(2) == RgbColor(0, 0, 0));

  // 4. Test with smoothTransitions = false (discrete step fill)
  cfg.smoothTransitions = false;
  ShiftLightEngine stepEngine(cfg);
  stepEngine.reset(0);
  stepEngine.update(100, 2575, ViewStatus::Live);
  CHECK(stepEngine.getPixel(0) == RgbColor(0, 255, 0));
  CHECK(stepEngine.getPixel(1) == RgbColor(0, 0, 0));  // discrete: partial is off

  // 5. Variable LED counts:
  // Single LED bar
  cfg.ledCount = 1;
  cfg.smoothTransitions = true;
  ShiftLightEngine singleEngine(cfg);
  singleEngine.reset(0);
  singleEngine.update(100, 1500, ViewStatus::Live);
  CHECK(singleEngine.getPixel(0) == RgbColor(0, 0, 0));
  singleEngine.update(150, 3650, ViewStatus::Live);  // 50%
  CHECK(singleEngine.getPixel(0).g > 80 && singleEngine.getPixel(0).g < 180);

  // 12 LEDs
  cfg.ledCount = 12;
  ShiftLightEngine twelveEngine(cfg);
  twelveEngine.reset(0);
  twelveEngine.update(100, 5800, ViewStatus::Live);
  // At 5800 RPM, all 12 LEDs are lit (or alert)
  CHECK(twelveEngine.ledCount() == 12);

  pass("progressive RPM illumination, smooth partial brightness, and variable LED counts");
}

void shift_light_color_styles() {
  auto cfg = defaultTestConfig();
  cfg.alertMode = ShiftAlertMode::Off;  // inspect progressive color bar at maximum fill
  cfg.rpmShift = 10000;
  ShiftLightEngine engine(cfg);
  engine.reset(0);

  // 1. CLASSIC style check: Green (0, 1) -> Yellow (2, 3) -> Orange (4) -> Red (5)
  cfg.style = ShiftLightStyle::Classic;
  engine.setStyle(ShiftLightStyle::Classic);
  engine.update(100, 10000, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 0));    // Green
  CHECK(engine.getPixel(1) == RgbColor(0, 255, 0));    // Green
  CHECK(engine.getPixel(2) == RgbColor(255, 200, 0));  // Yellow
  CHECK(engine.getPixel(3) == RgbColor(255, 200, 0));  // Yellow
  CHECK(engine.getPixel(4) == RgbColor(255, 80, 0));   // Orange
  CHECK(engine.getPixel(5) == RgbColor(255, 0, 0));    // Red

  // 2. RACING style check: Vivid Green (0, 1) -> Amber (2, 3) -> Crimson (4) -> Red (5)
  engine.setStyle(ShiftLightStyle::Racing);
  engine.update(200, 10000, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 60));
  CHECK(engine.getPixel(2) == RgbColor(255, 160, 0));
  CHECK(engine.getPixel(4) == RgbColor(255, 0, 90));
  CHECK(engine.getPixel(5) == RgbColor(255, 0, 0));

  // 3. MINIMAL style check: Ice White (0..4) -> Red (5)
  engine.setStyle(ShiftLightStyle::Minimal);
  engine.update(300, 10000, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(220, 235, 255));
  CHECK(engine.getPixel(4) == RgbColor(220, 235, 255));
  CHECK(engine.getPixel(5) == RgbColor(255, 0, 0));

  // 4. CUSTOM style check:
  cfg.style = ShiftLightStyle::Custom;
  cfg.customColors[0] = RgbColor{10, 20, 30};
  cfg.customColors[1] = RgbColor{40, 50, 60};
  ShiftLightEngine customEngine(cfg);
  customEngine.reset(0);
  customEngine.update(400, 10000, ViewStatus::Live);
  CHECK(customEngine.getPixel(0) == RgbColor(10, 20, 30));
  CHECK(customEngine.getPixel(1) == RgbColor(40, 50, 60));

  // 5. Master brightness scaling:
  engine.setStyle(ShiftLightStyle::Classic);
  engine.setMasterBrightness(128);  // ~50%
  engine.update(500, 10000, ViewStatus::Live);
  RgbColor p0 = engine.getPixel(0);
  CHECK(p0.g >= 127 && p0.g <= 129 && p0.r == 0 && p0.b == 0);

  engine.setMasterBrightness(0);
  engine.update(600, 10000, ViewStatus::Live);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 6. Reversed direction:
  cfg.style = ShiftLightStyle::Classic;
  cfg.masterBrightness = 255;
  cfg.direction = LedDirection::Reversed;
  ShiftLightEngine revEngine(cfg);
  revEngine.reset(0);
  revEngine.update(700, 10000, ViewStatus::Live);
  // In reverse order, index 0 is Red and index 5 is Green!
  CHECK(revEngine.getPixel(0) == RgbColor(255, 0, 0));
  CHECK(revEngine.getPixel(5) == RgbColor(0, 255, 0));

  pass("color styles (Classic, Racing, Minimal, Custom), brightness scaling, and direction reversal");
}

void shift_light_shift_alert_hysteresis() {
  auto cfg = defaultTestConfig();
  cfg.rpmShift = 5800;
  cfg.hysteresisRpm = 150;
  cfg.alertMode = ShiftAlertMode::FlashRed;
  ShiftLightEngine engine(cfg);
  engine.reset(0);

  // 1. Just below threshold (5799 RPM): alert inactive
  engine.update(100, 5799, ViewStatus::Live);
  CHECK(!engine.isAlertActive());

  // 2. Exactly at threshold (5800 RPM): alert triggers!
  engine.update(200, 5800, ViewStatus::Live);
  CHECK(engine.isAlertActive());

  // 3. Small oscillation drop (5780 RPM): hysteresis keeps alert ACTIVE
  engine.update(250, 5780, ViewStatus::Live);
  CHECK(engine.isAlertActive());

  // 4. Oscillate back up to 5820 RPM: still ACTIVE
  engine.update(300, 5820, ViewStatus::Live);
  CHECK(engine.isAlertActive());

  // 5. Drop to 5651 RPM (clear threshold is 5800 - 150 = 5650): still ACTIVE
  engine.update(350, 5651, ViewStatus::Live);
  CHECK(engine.isAlertActive());

  // 6. Drop to 5649 RPM (below 5650): alert DEACTIVATES!
  engine.update(400, 5649, ViewStatus::Live);
  CHECK(!engine.isAlertActive());

  // 7. Rise back to 5750 RPM (below 5800): stays inactive until 5800
  engine.update(450, 5750, ViewStatus::Live);
  CHECK(!engine.isAlertActive());

  // 8. Test alert modes:
  // FlashRed mode toggles on/off:
  engine.update(500, 5900, ViewStatus::Live);  // triggers alert at t=500
  CHECK(engine.isAlertActive());
  // At t=520 (elapsed 20ms into 100ms cycle: on)
  engine.update(520, 5900, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(255, 0, 0));
  // At t=570 (elapsed 70ms into 100ms cycle: off)
  engine.update(570, 5900, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 0, 0));

  // SolidRed mode:
  engine.setAlertMode(ShiftAlertMode::SolidRed);
  engine.update(600, 5900, ViewStatus::Live);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(255, 0, 0));

  // FlashWhite mode:
  engine.setAlertMode(ShiftAlertMode::FlashWhite);
  engine.update(720, 5900, ViewStatus::Live);  // t=720: elapsed 220 % 100 = 20ms (on)
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(255, 255, 255));

  // Off mode (alert disabled, progressive fill shown):
  engine.setAlertMode(ShiftAlertMode::Off);
  engine.update(800, 5900, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 0));  // classic green at bottom

  pass("shift alert activation, hysteresis oscillation damping, and alert visual modes");
}

void shift_light_startup_sequence() {
  auto cfg = defaultTestConfig();
  cfg.startupAnimationEnabled = true;
  ShiftLightEngine engine(cfg);
  engine.reset(1000);  // start animation at t=1000
  CHECK(!engine.isStartupComplete());

  // Sweep 1: forward sweep (0..400ms -> t=1000..1400)
  engine.update(1100, 3000, ViewStatus::Live);
  CHECK(!engine.isStartupComplete());
  // Active LED should be around index 1 (100ms / 400ms * 6 = 1.5)
  CHECK(engine.getPixel(1) != RgbColor(0, 0, 0));

  // Sweep 2: reverse sweep (400..800ms -> t=1400..1800)
  engine.update(1500, 3000, ViewStatus::Live);
  CHECK(!engine.isStartupComplete());

  // Pulse: (800..1100ms -> t=1800..2100)
  engine.update(1950, 3000, ViewStatus::Live);
  CHECK(!engine.isStartupComplete());
  // At peak of pulse (150ms in), pixels are illuminated
  CHECK(engine.getPixel(0) != RgbColor(0, 0, 0));

  // Completion: (>1200ms -> t>=2200)
  engine.update(2201, 3000, ViewStatus::Live);
  CHECK(engine.isStartupComplete());

  // Interruption by CAN Fault during startup:
  engine.reset(3000);
  CHECK(!engine.isStartupComplete());
  engine.update(3100, 3000, ViewStatus::Fault);  // fault occurs!
  CHECK(engine.isStartupComplete());             // immediately completed/aborted
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // Monotonic timer rollover test:
  engine.reset(0xFFFFFFF0);
  CHECK(!engine.isStartupComplete());
  // After 1300 ms across 32-bit rollover: (0xFFFFFFF0 + 1300) = 0x00000504
  engine.update(0x00000504, 3000, ViewStatus::Live);
  CHECK(engine.isStartupComplete());

  pass("non-blocking startup animation, phase progression, fault interruption, and timer rollover");
}

void shift_light_faults_and_safety() {
  auto cfg = defaultTestConfig();
  ShiftLightEngine engine(cfg);
  engine.reset(0);

  // 1. Establish valid high RPM with active shift alert
  engine.update(100, 6000, ViewStatus::Live);
  CHECK(engine.isAlertActive());

  // 2. Data becomes Stale: alert MUST clear immediately, LEDs MUST turn off
  engine.update(200, 6000, ViewStatus::Stale);
  CHECK(!engine.isAlertActive());
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 3. Disconnected: LEDs stay off
  engine.update(300, 6000, ViewStatus::Disconnected);
  CHECK(!engine.isAlertActive());
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 4. Invalid frame / unmapped: LEDs stay off
  engine.update(400, 6000, ViewStatus::Invalid);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  engine.update(500, 6000, ViewStatus::Unmapped);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 5. Fault: LEDs stay off
  engine.update(600, 6000, ViewStatus::Fault);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // 6. Recovery: valid data resumes at 4000 RPM -> progressive fill restores
  engine.update(700, 4000, ViewStatus::Live);
  CHECK(engine.getPixel(0) == RgbColor(0, 255, 0));

  pass("fail-safe blanking on stale, disconnected, invalid, unmapped, and fault states");
}

void shift_light_idle_effect() {
  auto cfg = defaultTestConfig();
  cfg.idleEffectEnabled = true;
  cfg.idleMaxRpm = 1300;
  ShiftLightEngine engine(cfg);
  engine.reset(0);

  // Live at 1000 RPM (idle): LED 0 breathes, remaining LEDs off
  engine.update(100, 1000, ViewStatus::Live);
  CHECK(engine.getPixel(0) != RgbColor(0, 0, 0));
  for (uint8_t i = 1; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  // Disconnected at 1000 RPM: ALL LEDs off (idle does not mask disconnection)
  engine.update(200, 1000, ViewStatus::Disconnected);
  for (uint8_t i = 0; i < 6; ++i) CHECK(engine.getPixel(i) == RgbColor(0, 0, 0));

  pass("idle breathing effect active only during live reception below idle threshold");
}

void shift_light_replay_manual_control() {
  ReplayTransport replay;
  CanFrame f;

  // 1. Manual synthetic RPM injection
  replay.setManualRpm(4200);
  CHECK(replay.manualRpm() == 4200);
  bool got = replay.receive(100, f);
  CHECK(got);
  uint16_t rpm = uint16_t(f.data[0]) | (uint16_t(f.data[1]) << 8);
  CHECK(rpm == 4200);

  // 2. Pause demonstration
  replay.setManualRpm(-1);  // return to auto cycle
  replay.setPaused(true);
  CHECK(replay.isPaused());
  CHECK(!replay.receive(200, f));

  // 3. Resume demonstration
  replay.setPaused(false);
  CHECK(replay.receive(300, f));

  pass("ReplayTransport manual synthetic RPM injection and pause/resume control");
}

} // namespace

int main() {
  decoding();
  envelope();
  decode_names();
  state();
  leds();
  tx();
  replay();
  shift_light_progressive_fill();
  shift_light_color_styles();
  shift_light_shift_alert_hysteresis();
  shift_light_startup_sequence();
  shift_light_faults_and_safety();
  shift_light_idle_effect();
  shift_light_replay_manual_control();
  std::printf("ALL PASS: %u cases, %u assertions\n", cases, assertions);
  return 0;
}
