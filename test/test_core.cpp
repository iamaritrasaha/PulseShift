// Core logic tests: config validation/serialization, calibration session fit,
// staged transactions, event pacing and serial parsing helpers.
// Pure host build: no Arduino stubs needed (config_manager uses the chrono
// millis() fallback when compiled without ARDUINO / PULSESHIFT_TEST_BUILD).
#include "event_summary.h"
#include "serial_protocol.h"
#include "led_color.h"
#include "rpm_candidate.h"
#include "config_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>

#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"CORE FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static unsigned checks=0;

static void event_summary_pacing() {
  EventSummary s;
  CHECK(s.total()==0 && s.take(0,1000)==0);
  s.record(); s.record(); s.record();
  CHECK(s.total()==3 && s.take(500,1000)==0);      // interval not elapsed
  CHECK(s.take(1000,1000)==3);                     // flushes all pending
  CHECK(s.total()==3 && s.take(1100,1000)==0);     // nothing pending
}

static void serial_parse_helpers() {
  uint32_t v = 123;
  CHECK(parseUnsignedNumber(" 4500\t", 20000, v) && v==4500);
  v = 123;
  CHECK(!parseUnsignedNumber("20001", 20000, v) && v==123);
  CHECK(!parseUnsignedNumber("-5", 20000, v));
  CHECK(parseUnsignedNumber("0", 0, v) && v==0);
  CHECK(!parseUnsignedNumber("1", 0, v));
  CHECK(parseUnsignedNumber("4294967295", UINT32_MAX, v) && v==UINT32_MAX);

  SerialProtocol proto;
  for (char c : std::string("STATUS")) CHECK(!proto.feed(c));
  CHECK(proto.feed('\r') && std::strcmp(proto.command(),"STATUS")==0);
  proto.reset();
  // Overlong command must be dropped, not truncated.
  for (unsigned i=0;i<200;++i) CHECK(!proto.feed('x'));
  CHECK(!proto.feed('\n'));

  // Color scaling: 255 is identity, rounding is symmetric.
  const RgbColor c{100, 200, 255};
  CHECK(scaleColor(c, 255) == c);
  const RgbColor half = scaleColor(c, 128);
  CHECK(half.r==50 && half.g==100 && half.b==128);
}

static void config_validate() {
  PulseShiftConfig cfg; // default member initializers == documented safe defaults
  CHECK(ConfigManager::validateConfig(cfg));

  // RPM bar without a bound verification record is invalid (safety rule).
  cfg.effect = 4;
  CHECK(!ConfigManager::validateConfig(cfg));
  cfg.effect = 0;

  cfg.rpmThresholds[3] = 500; // breaks ascending order
  CHECK(!ConfigManager::validateConfig(cfg));

  cfg = PulseShiftConfig{};
  cfg.rpmMultiplier = 0.05f;
  CHECK(!ConfigManager::validateConfig(cfg));
  cfg.rpmMultiplier = 1000.0f;
  CHECK(ConfigManager::validateConfig(cfg));

  cfg = PulseShiftConfig{};
  cfg.candidateByteWidth = 2;
  cfg.candidateByteIndex = 7; // 7 + 2 > 8
  CHECK(!ConfigManager::validateConfig(cfg));

  cfg = PulseShiftConfig{};
  cfg.canCandidateId = 0x800; // standard frame must stay within 11 bits
  CHECK(!ConfigManager::validateConfig(cfg));

  cfg = PulseShiftConfig{};
  cfg.warningPatternId = 4;
  CHECK(!ConfigManager::validateConfig(cfg));
}

static void snapshot_serialization_roundtrip() {
  ConfigManager& cm = ConfigManager::instance();
  CHECK(cm.restoreDefaults(0xA5, 0x5A)); // normalize to safe defaults

  PulseShiftConfig& cfg = cm.activeMutable();
  cfg.brightness = 123;
  cfg.rpmThresholds[2] = 5200;
  cfg.rpmHysteresis = 300;

  uint8_t buf[512];
  const size_t len = cm.serializeSnapshot(buf, sizeof(buf));
  CHECK(len > 40 && len <= sizeof(buf));

  // Header: schema byte (=3) + revision u32
  CHECK(buf[0] == 3);

  // Walk TLVs and verify the mandatory schema-3 set is present, then CRC32.
  size_t off = 5;
  bool sawHysteresis=false, sawDecoderExt=false, sawCalRecord=false, sawThresholds=false;
  while (off + 2 <= len - 4) {
    const uint8_t id = buf[off];
    const uint8_t tl = buf[off+1];
    CHECK(off + 2 + tl <= len - 4);
    if (id == 0x16 && tl == 12) {
      sawThresholds = true;
      const uint16_t t2 = uint16_t(buf[off+2+4]) | uint16_t(uint16_t(buf[off+2+5]) << 8);
      CHECK(t2 == 5200);
    }
    if (id == 0x1C && tl == 8) sawDecoderExt = true;
    if (id == 0x1D && tl == 11) sawCalRecord = true;
    if (id == 0x1E && tl == 2) {
      sawHysteresis = true;
      const uint16_t h = uint16_t(buf[off+2]) | uint16_t(uint16_t(buf[off+3]) << 8);
      CHECK(h == 300);
    }
    off += 2 + tl;
  }
  CHECK(sawThresholds && sawDecoderExt && sawCalRecord && sawHysteresis);

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i=0;i<len-4;++i) {
    crc ^= buf[i];
    for (uint8_t b=0;b<8;++b) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  crc ^= 0xFFFFFFFF;
  const uint32_t stored = uint32_t(buf[len-4]) | uint32_t(uint32_t(buf[len-3]) << 8) |
                          uint32_t(uint32_t(buf[len-2]) << 16) | uint32_t(uint32_t(buf[len-1]) << 24);
  CHECK(crc == stored);

  CHECK(cm.restoreDefaults(0xA5, 0x5A)); // leave clean defaults for later cases
}

static void decoder_fingerprint() {
  RpmDecoderSettings a; // defaults: 0x301, byte 0, x40.0
  RpmDecoderSettings b = a;
  CHECK(computeDecoderFingerprint(a) == computeDecoderFingerprint(b));
  b.multiplier = 41.0f;
  CHECK(computeDecoderFingerprint(a) != computeDecoderFingerprint(b));
  b = a;
  b.canId = 0x302;
  CHECK(computeDecoderFingerprint(a) != computeDecoderFingerprint(b));
  b = a;
  b.byteWidth = 2;
  CHECK(computeDecoderFingerprint(a) != computeDecoderFingerprint(b));
  b = a;
  b.offset = 5.0f;
  CHECK(computeDecoderFingerprint(a) != computeDecoderFingerprint(b));
}

static void calibration_session_fit_and_bind() {
  ConfigManager& cm = ConfigManager::instance();
  CHECK(cm.restoreDefaults(0xA5, 0x5A));
  const uint32_t t0 = 100000;

  // A session that was never begun rejects commits and compute.
  CHECK(cm.commitCalibrationPoint(4000, t0) == BLE_CFG_STATUS_INVALID_STATE);
  const CalibrationResult early = cm.computeCalibration(false, t0);
  CHECK(early.status == BLE_CFG_STATUS_INVALID_STATE);

  // Three stable, monotonic operating points: raw 100->4000, 200->8000, 300->12000.
  struct Pt { uint16_t raw; uint16_t ref; };
  const Pt pts[] = {{100,4000},{200,8000},{300,12000}};
  CHECK(cm.beginCalibrationSession(t0));
  for (const Pt& p : pts) {
    CHECK(cm.startCalibrationCapture(t0 + 1));
    for (unsigned i=0;i<30;++i) cm.feedCalibrationSample(p.raw, t0 + 10 + i);
    CHECK(cm.calibrationCaptureSamples() >= 10);
    CHECK(cm.commitCalibrationPoint(p.ref, t0 + 500) == BLE_CFG_STATUS_OK);
  }
  CHECK(cm.calibrationPointCount() == 3);

  // Fit-only preview must not mutate the active config.
  const CalibrationResult preview = cm.computeCalibration(false, t0 + 600);
  CHECK(preview.status == BLE_CFG_STATUS_OK);
  CHECK(std::fabs(preview.multiplier - 40.0f) < 0.01f);
  CHECK(std::fabs(preview.offset) < 1.0f);
  CHECK(!cm.active().calibrationVerified);

  // Apply must bind the verification record to the resulting fingerprint.
  const CalibrationResult applied = cm.computeCalibration(true, t0 + 700);
  CHECK(applied.status == BLE_CFG_STATUS_OK);
  CHECK(cm.active().calibrationVerified);
  CHECK(cm.active().calPointsUsed == 3);
  CHECK(cm.active().calFingerprint != 0);
  RpmDecoderSettings s;
  s.canId = cm.active().canCandidateId;
  s.byteIndex = cm.active().candidateByteIndex;
  s.byteWidth = cm.active().candidateByteWidth;
  s.littleEndian = cm.active().candidateLittleEndian;
  s.multiplier = cm.active().rpmMultiplier;
  s.offset = cm.active().rpmOffset;
  s.maxRpm = cm.active().rpmMaxValid;
  CHECK(cm.active().calFingerprint == computeDecoderFingerprint(s));

  // With the record bound, the RPM bar effect becomes valid and the
  // safeguard can qualify a matching snapshot.
  PulseShiftConfig checkCfg = cm.active();
  checkCfg.effect = 4;
  CHECK(ConfigManager::validateConfig(checkCfg));
}

static void calibration_rejects_bad_evidence() {
  ConfigManager& cm = ConfigManager::instance();
  CHECK(cm.restoreDefaults(0xA5, 0x5A)); // clear the record bound by the previous case
  const uint32_t t0 = 500000;
  CHECK(cm.beginCalibrationSession(t0));

  // Non-monotonic evidence (higher reference but lower raw) must be rejected.
  struct Pt { uint16_t raw; uint16_t ref; };
  const Pt pts[] = {{100,2000},{150,6000},{120,10000}};
  for (const Pt& p : pts) {
    CHECK(cm.startCalibrationCapture(t0+1));
    for (unsigned i=0;i<20;++i) cm.feedCalibrationSample(p.raw, t0+10+i);
    CHECK(cm.commitCalibrationPoint(p.ref, t0+400) == BLE_CFG_STATUS_OK);
  }
  const CalibrationResult r = cm.computeCalibration(false, t0+500);
  CHECK(r.status == BLE_CFG_STATUS_VALIDATION_FAILED);
  CHECK(!cm.active().calibrationVerified);
  cm.abortCalibrationSession();
}

static void staged_transactions() {
  ConfigManager& cm = ConfigManager::instance();
  CHECK(cm.restoreDefaults(0xA5, 0x5A));
  const uint32_t rev0 = cm.activeRevision();
  CHECK(cm.beginTransaction());
  CHECK(cm.isStagedTransactionActive());
  CHECK(cm.setBrightness(77, true) == BLE_CFG_STATUS_OK);
  CHECK(cm.active().brightness == 200); // staged copy untouched until commit
  CHECK(cm.commitTransaction());
  CHECK(cm.active().brightness == 77);
  CHECK(cm.activeRevision() == rev0 + 1);
  CHECK(!cm.isStagedTransactionActive());

  // Validation failure rolls back the apply but KEEPS the transaction open
  // (the client may fix staged fields and retry, or abort explicitly).
  CHECK(cm.beginTransaction());
  CHECK(cm.setLedThreshold(1, 50, true) == BLE_CFG_STATUS_OK);
  CHECK(!cm.commitTransaction());
  CHECK(cm.active().rpmThresholds[1] == 4000);
  CHECK(cm.active().brightness == 77); // failed commit keeps previous state
  CHECK(cm.isStagedTransactionActive());
  cm.abortTransaction();
  CHECK(!cm.isStagedTransactionActive());
  CHECK(cm.active().rpmThresholds[1] == 4000); // abort leaves active untouched

  CHECK(cm.restoreDefaults(0xA5, 0x5A)); // leave clean defaults
}

static void restore_defaults_preserves_shape() {
  ConfigManager& cm = ConfigManager::instance();
  CHECK(!cm.restoreDefaults(0x00, 0x00)); // wrong confirmation bytes rejected
  CHECK(cm.restoreDefaults(0xA5, 0x5A));
  const PulseShiftConfig& cfg = cm.active();
  CHECK(cfg.brightness == 200);
  CHECK(cfg.rpmMultiplier == 40.0f);
  CHECK(cfg.canCandidateId == 0x301);
  CHECK(!cfg.calibrationVerified); // fresh defaults are strictly unverified
}

static void candidate_monitor_staleness() {
  // The LEDs must never keep rendering the last RPM after CAN disappears:
  // the monitor marks samples stale once the freshness timeout elapses.
  RpmCandidateMonitor m; // 250 ms freshness
  CanFrame f{};
  f.id = 0x301; f.dlc = 8; f.data[0] = 44;
  CHECK(m.ingest(f, 1000));
  CHECK(m.isFresh(1100));
  CHECK(m.estimatedRpm(1100) == 44 * 40);
  CHECK(!m.isFresh(1400)); // 400 ms > 250 ms timeout
  CHECK(m.estimatedRpm(1400) == 0);
  const auto snap = m.snapshot(1500);
  CHECK(!snap.fresh && !snap.valid && snap.rpm == 0);

  // Wrong CAN ID / DLC rejected.
  CanFrame bad{}; bad.id = 0x302; bad.dlc = 8; bad.data[0] = 44;
  CHECK(!m.ingest(bad, 1600));
  CanFrame short_{}; short_.id = 0x301; short_.dlc = 7;
  CHECK(!m.ingest(short_, 1601));
}

int main() {
  event_summary_pacing();
  serial_parse_helpers();
  config_validate();
  snapshot_serialization_roundtrip();
  decoder_fingerprint();
  calibration_session_fit_and_bind();
  calibration_rejects_bad_evidence();
  staged_transactions();
  restore_defaults_preserves_shape();
  candidate_monitor_staleness();
  std::printf("PASS config state machine, calibration binding, serialization, pacing: %u checks\n", checks);
  return 0;
}
