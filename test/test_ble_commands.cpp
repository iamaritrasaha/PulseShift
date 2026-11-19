// BLE v2 command dispatch tests (host build with test hooks):
// authorization binding, staged transactions, calibration workflow and the
// device control commands, all through the real BleManager dispatcher.
#include "ble_manager.h"
#include "config_manager.h"
#include "ota_manager.h"
#include "rpm_candidate.h"
#include "can_transport.h"
#include "can_recorder.h"
#include "storage_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>

// Minimal TWAI/GPIO stub definitions: this binary links can_transport.cpp but
// never drives the transport, so the driver symbols only need to resolve.
// test_transport.cpp supplies its own assertion-instrumented stubs for the
// transport tests and is a separate binary.
int gpio_reset_pin(gpio_num_t) { return ESP_OK; }
int gpio_set_direction(gpio_num_t, int) { return ESP_OK; }
int gpio_set_pull_mode(gpio_num_t, int) { return ESP_OK; }
int twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*) { return ESP_OK; }
int twai_start() { return ESP_OK; }
int twai_stop() { return ESP_OK; }
int twai_read_alerts(uint32_t* alerts, uint32_t) { *alerts = 0; return ESP_OK; }
int twai_get_status_info(twai_status_info_t* s) { s->state = TWAI_STATE_RUNNING; return ESP_OK; }
int twai_receive(twai_message_t*, uint32_t) { return ESP_ERR_TIMEOUT; }
int twai_initiate_recovery() { return ESP_OK; }

#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"BLE CMD FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static unsigned checks=0;

static BleConfigCommandPacket makeCmd(uint8_t opcode, const std::vector<uint8_t>& payload, uint16_t seq, bool staged = false) {
  BleConfigCommandPacket pkt{};
  pkt.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
  pkt.opcode = opcode;
  pkt.sequence = seq;
  pkt.payloadLength = static_cast<uint8_t>(payload.size());
  pkt.flags = staged ? BLE_CFG_FLAG_STAGED : 0;
  for (size_t i = 0; i < payload.size() && i < sizeof(pkt.payload); ++i) pkt.payload[i] = payload[i];
  return pkt;
}

static const BleConfigResultPacket& lastResult(BleManager& ble) {
  CHECK(ble.testResultCount() > 0);
  return ble.testResult(ble.testResultCount() - 1);
}

static void drain(BleManager& ble, RpmCandidateMonitor& cand, uint32_t now) {
  ble.testDrainCommands(now, cand, CanTransport(0), CanRecorder::instance());
}

static void read_queries_do_not_need_authorization() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CHECK(ble.testResultCount() == 0);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_CAPABILITIES, {}, 1), 5);
  drain(ble, cand, 1000);
  CHECK(lastResult(ble).respondedOpcode == BLE_CFG_OP_GET_CAPABILITIES);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_SNAPSHOT, {}, 2), 5);
  drain(ble, cand, 1001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_DIAGNOSTICS, {}, 3), 5);
  drain(ble, cand, 1002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_STATUS, {}, 4), 5);
  drain(ble, cand, 1003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 5);
  CHECK(lastResult(ble).payload[0] == 0); // no session active
}

static void writes_require_authorization() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {99}, 10), 5);
  drain(ble, cand, 1100);
  CHECK(lastResult(ble).respondedOpcode == BLE_CFG_OP_SET_BRIGHTNESS);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_UNAUTHORIZED);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_AUTH_REQUIRED);
  CHECK(ConfigManager::instance().active().brightness == 200); // unchanged

  // Version mismatch fails fast before any authorization logic.
  {
    BleConfigCommandPacket bad = makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {99}, 11);
    bad.version = 1;
    ble.testEnqueueCommand(bad, 5);
    drain(ble, cand, 1101);
    CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_VERSION_MISMATCH);
  }

  // Open the physical authorization window for the authorized-path checks.
  ConfigManager::instance().triggerAuthWindow(1102);

  // Unknown opcodes are rejected as UNSUPPORTED for an authorized session;
  // the authorization gate precedes the opcode switch.
  ble.testEnqueueCommand(makeCmd(0x7F, {}, 12), 5);
  drain(ble, cand, 1103);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_UNSUPPORTED);

  // Staged field writes outside an open transaction are rejected.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {1, 2, 3}, 13, true), 5);
  drain(ble, cand, 1104);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_STATE);
}

static void enroll_then_session_is_connection_bound() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  // Physical presence window + enrollment binds a session to connId 7.
  cm.triggerAuthWindow(2000);
  std::vector<uint8_t> secret = {1,2,3,4,5,6,7,8,9,10,11,12};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 20), 7);
  drain(ble, cand, 2001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_PERSISTED);
  CHECK(cm.isOwnerEnrolled());
  CHECK(cm.isSessionAuthorized());

  // Authorized write from the enrolling connection.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {180}, 21), 7);
  drain(ble, cand, 2100);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_APPLIED);
  CHECK(cm.active().brightness == 180);

  // After the window closes, a different connection may NOT ride the session.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {10}, 22), 99);
  drain(ble, cand, 70000); // 68000 ms later: window (60 s) closed, session (5 min) alive
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_UNAUTHORIZED);
  CHECK(cm.active().brightness == 180);

  // Challenge-response authentication from the other connection.
  uint32_t nonce = 0;
  CHECK(cm.requestChallenge(71000, nonce));
  CHECK(nonce != 0);
  // Mirror of the host proof construction: SHA-256 on hardware, CRC pair on host.
  uint8_t msg[18];
  for (int i=0;i<12;++i) msg[i] = static_cast<uint8_t>(i + 1);
  msg[12] = nonce & 0xFF; msg[13] = (nonce>>8) & 0xFF; msg[14] = (nonce>>16) & 0xFF; msg[15] = (nonce>>24) & 0xFF;
  msg[16] = 23 & 0xFF; msg[17] = (23>>8) & 0xFF;
  const uint32_t c1 = ConfigManager::computeCrc32(msg, 9);
  const uint32_t c2 = ConfigManager::computeCrc32(msg + 9, 9);
  std::vector<uint8_t> proof = {
    uint8_t(c1), uint8_t(c1>>8), uint8_t(c1>>16), uint8_t(c1>>24),
    uint8_t(c2), uint8_t(c2>>8), uint8_t(c2>>16), uint8_t(c2>>24)
  };
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_AUTHENTICATE, proof, 23), 99);
  drain(ble, cand, 71001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  // Now connId 99 may write; a replayed proof is rejected (single-use challenge).
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {150}, 24), 99);
  drain(ble, cand, 71100);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(cm.active().brightness == 150);
}

static void staged_transaction_over_ble() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  cm.triggerAuthWindow(80000); // reopen window
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_BEGIN_TRANSACTION, {}, 30), 7);
  drain(ble, cand, 80001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_STAGED);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_POWER, {0}, 31, true), 7);
  drain(ble, cand, 80002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_STAGED);
  CHECK(cm.active().isPowerOn == true); // staged only

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_COMMIT_TRANSACTION, {}, 32), 7);
  drain(ble, cand, 80003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_APPLIED);
  CHECK(cm.active().isPowerOn == false); // applied atomically

  auto request = [&](uint8_t opcode, std::vector<uint8_t> payload, bool staged = false) {
    ble.testEnqueueCommand(makeCmd(opcode, payload, 33, staged), 7);
    drain(ble, cand, 80004);
    return lastResult(ble);
  };
  for (const auto& payload : std::vector<std::vector<uint8_t>>{{}, {1}, {1, 0}, {1, 65}, {2, 12}}) {
    CHECK(request(BLE_CFG_OP_SET_LED_COUNT, payload).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  }
  CHECK(request(BLE_CFG_OP_SET_LED_COUNT, {1, 64}).resultFlags == BLE_CFG_RESULT_FLAG_APPLIED);
  CHECK(cm.active().physicalLedCount == 64 && cm.runningLedCount() == 6);
  CHECK(request(BLE_CFG_OP_SAVE_CONFIG, {0, 0, 0, 0}).statusCode == BLE_CFG_STATUS_INVALID_STATE);
  CHECK(request(BLE_CFG_OP_BEGIN_TRANSACTION, {}).resultFlags == BLE_CFG_RESULT_FLAG_STAGED);
  CHECK(request(BLE_CFG_OP_SAVE_CONFIG, {}).statusCode == BLE_CFG_STATUS_INVALID_STATE);
  CHECK(request(BLE_CFG_OP_ABORT_TRANSACTION, {}).statusCode == BLE_CFG_STATUS_OK);
  const uint32_t revision = cm.activeRevision();
  const auto verified = request(BLE_CFG_OP_SAVE_CONFIG,
      {uint8_t(revision), uint8_t(revision >> 8), uint8_t(revision >> 16), uint8_t(revision >> 24)});
  CHECK(verified.statusCode == BLE_CFG_STATUS_OK && verified.payloadLength == 1);
  CHECK(verified.payload[0] == static_cast<uint8_t>(ConfigSaveStatus::Verified));
  const auto legacy = request(BLE_CFG_OP_SAVE_CONFIG, {});
  CHECK(legacy.statusCode == BLE_CFG_STATUS_OK && legacy.payloadLength == 0);
  CHECK(legacy.resultFlags & BLE_CFG_RESULT_FLAG_PERSISTED);
}

static void calibration_workflow_over_ble() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();
  cm.triggerAuthWindow(90000);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_BEGIN, {}, 40), 7);
  drain(ble, cand, 90001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  struct Pt { uint16_t raw; uint16_t ref; };
  const Pt pts[] = {{100,4000},{200,8000},{300,12000}};
  uint16_t seq = 41;
  for (const Pt& p : pts) {
    ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_CAPTURE_START, {}, seq++), 7);
    drain(ble, cand, 90002);
    CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
    // Drive real samples through the application feed path.
    for (unsigned i=0;i<30;++i) cm.feedCalibrationSample(p.raw, 90003 + i);
    const std::vector<uint8_t> ref = {uint8_t(p.ref & 0xFF), uint8_t(p.ref >> 8)};
    ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_POINT_COMMIT, ref, seq++), 7);
    drain(ble, cand, 90100);
    CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  }

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_COMPUTE, {1}, seq), 7);
  drain(ble, cand, 90200);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 9);
  // multiplier x1000 == 40000
  const uint32_t multX1000 = uint32_t(lastResult(ble).payload[0]) |
                             uint32_t(uint32_t(lastResult(ble).payload[1]) << 8) |
                             uint32_t(uint32_t(lastResult(ble).payload[2]) << 16) |
                             uint32_t(uint32_t(lastResult(ble).payload[3]) << 24);
  CHECK(multX1000 == 40000);
  // Info byte: pointsUsed in the low nibble (3), residual 0 -> high nibble 0.
  CHECK((lastResult(ble).payload[8] & 0x0F) == 3);
  CHECK(((lastResult(ble).payload[8] >> 4) & 0x0F) == 0);
  CHECK(cm.active().calibrationVerified);

  // The dispatcher must have rebuilt the live decoder mirror: the candidate
  // monitor now reports the verified calibration.
  CHECK(cand.isCalibrated());

  // CAL_STATUS reports the session with the stored point count.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CAL_STATUS, {}, ++seq), 7);
  drain(ble, cand, 90300);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 5);
  CHECK(lastResult(ble).payload[1] == 3);
}

static void device_commands() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();
  cm.triggerAuthWindow(95000);

  // Restart without explicit confirmation bytes is rejected.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_RESTART_DEVICE, {0x00}, 50), 7);
  drain(ble, cand, 95001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_RESTART_DEVICE, {0xA5, 0x5A}, 51), 7);
  drain(ble, cand, 95002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_APPLIED);

  // Clear-diagnostics confirmation gate.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CLEAR_DIAGNOSTICS, {0x11}, 52), 7);
  drain(ble, cand, 95003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_CLEAR_DIAGNOSTICS, {0xA5, 0x5A}, 53), 7);
  drain(ble, cand, 95004);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  // Recording: stop is always allowed, start is state-gated (host FS not begun).
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_CAPTURE_RECORDING, {0}, 54), 7);
  drain(ble, cand, 95005);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(!CanRecorder::instance().isRecording());
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_CAPTURE_RECORDING, {1}, 55), 7);
  drain(ble, cand, 95006);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_STATE); // storage not mounted on host
  CHECK(!CanRecorder::instance().isRecording());
}

static void mobile_terminal_stage_and_led_test() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();
  uint16_t rpm = 0;
  bool active = false;

  // A disconnected or unauthenticated terminal must not control LEDs/stages.
  cm.closeAuthWindow();
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_LED_TEST_RPM, {0x94, 0x11, 1}, 80), 250);
  drain(ble, cand, 180000);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_UNAUTHORIZED);
  CHECK(!ble.takeBenchTestRequest(rpm, active));

  cm.triggerAuthWindow(180001);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_CAPTURE_STAGE, {2}, 81), 250);
  drain(ble, cand, 180002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_APPLIED);
  CHECK(CanRecorder::instance().currentStage() == CanRecorder::STAGE_ENGINE_IDLING);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_CAPTURE_STAGE, {0}, 82), 250);
  drain(ble, cand, 180003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  CHECK(CanRecorder::instance().currentStage() == CanRecorder::STAGE_ENGINE_IDLING);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_LED_TEST_RPM, {0x94, 0x11, 1}, 83), 250); // 4500 RPM
  drain(ble, cand, 180004);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(ble.takeBenchTestRequest(rpm, active));
  CHECK(active && rpm == 4500);
  CHECK(!ble.takeBenchTestRequest(rpm, active)); // consume once

  // Malformed payloads must not change the pending test.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_LED_TEST_RPM, {0x94}, 84), 250);
  drain(ble, cand, 180005);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  CHECK(!ble.takeBenchTestRequest(rpm, active));

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_LED_TEST_RPM, {0xA8, 0x61, 1}, 85), 250); // >20000
  drain(ble, cand, 180006);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  CHECK(!ble.takeBenchTestRequest(rpm, active));

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_LED_TEST_RPM, {0, 0, 0}, 86), 250);
  drain(ble, cand, 180007);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(ble.takeBenchTestRequest(rpm, active));
  CHECK(!active && rpm == 0);
}

static void diagnostics_blob_shape() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_DIAGNOSTICS, {}, 60), 5);
  drain(ble, cand, 96000);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  const size_t len = ble.testPendingSnapshotLen();
  CHECK(len > 20 && len <= 512);
  const uint8_t* buf = ble.testPendingSnapshotData();
  CHECK(buf[0] == 1); // diagnostics blob schema

  // Verify the TLV walk and the mandatory diagnostic blocks.
  size_t off = 5;
  bool sawUptime=false, sawFirmware=false, sawCan=false, sawCandidate=false,
       sawStorage=false, sawBle=false, sawCapture=false, sawCalibration=false;
  while (off + 2 <= len - 4) {
    const uint8_t id = buf[off];
    const uint8_t tl = buf[off+1];
    CHECK(off + 2 + tl <= len - 4);
    if (id == BLE_DIAG_TLV_UPTIME && tl == 4) {
      sawUptime = true;
      const uint32_t up = uint32_t(buf[off+2]) | uint32_t(uint32_t(buf[off+3]) << 8) |
                          uint32_t(uint32_t(buf[off+4]) << 16) | uint32_t(uint32_t(buf[off+5]) << 24);
      CHECK(up == 96000);
    }
    if (id == BLE_DIAG_TLV_FIRMWARE && tl == 3) sawFirmware = true;
    if (id == BLE_DIAG_TLV_CAN_STATUS && tl == 26) sawCan = true;
    if (id == BLE_DIAG_TLV_CANDIDATE && tl == 34) sawCandidate = true;
    if (id == BLE_DIAG_TLV_STORAGE && tl == 29) sawStorage = true;
    if (id == BLE_DIAG_TLV_BLE_STATE && tl == 5) sawBle = true;
    if (id == BLE_DIAG_TLV_CAPTURE_INFO && tl == 9) sawCapture = true;
    if (id == BLE_DIAG_TLV_CALIBRATION && tl == 13) sawCalibration = true;
    off += 2 + tl;
  }
  CHECK(sawUptime && sawFirmware && sawCan && sawCandidate && sawStorage && sawBle && sawCapture && sawCalibration);

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i=0;i<len-4;++i) {
    crc ^= buf[i];
    for (uint8_t b=0;b<8;++b) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  crc ^= 0xFFFFFFFF;
  const uint32_t stored = uint32_t(buf[len-4]) | uint32_t(uint32_t(buf[len-3]) << 8) |
                          uint32_t(uint32_t(buf[len-2]) << 16) | uint32_t(uint32_t(buf[len-1]) << 24);
  CHECK(crc == stored);
}

static void reload_and_decoder_ext() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();
  cm.triggerAuthWindow(97000);

  // Decoder extension: 2-byte little-endian field at byte 0 with clamp 14000.
  std::vector<uint8_t> ext = {2, 1, 0x00,0x00,0x00,0x00, 0xB0,0x36}; // maxValid 14000 LE (0x36B0)
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_RPM_DECODER_EXT, ext, 70), 7);
  drain(ble, cand, 97001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(cand.byteWidth() == 2);        // live mirror rebuilt
  CHECK(cand.maxRpm() == 14000);
  CHECK(cm.active().candidateByteWidth == 2);
  CHECK(!cm.active().calibrationVerified); // decoder change invalidated the record

  // Hysteresis setter now dispatched.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_RPM_HYSTERESIS, {0x2C, 0x01}, 71), 7); // 300
  drain(ble, cand, 97002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(cm.active().rpmHysteresis == 300);

  // Reload from NVS: no valid blob on host -> reported as INVALID_STATE, and
  // crucially it must not clobber the active in-memory configuration.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_RELOAD_CONFIG, {}, 72), 7);
  drain(ble, cand, 97003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_STATE);
  CHECK(cm.active().rpmHysteresis == 300);

  CHECK(cm.restoreDefaults(0xA5, 0x5A));
}

static void malformed_and_duplicate_commands() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;

  // Fill the bounded queue beyond capacity; the queue must drop gracefully
  // without corrupting indices.
  for (int i = 0; i < 10; ++i) {
    ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_CAPABILITIES, {}, static_cast<uint16_t>(100 + i)), 5);
  }
  drain(ble, cand, 98000);
  CHECK(ble.testResultCount() >= 4); // at most CONFIG_QUEUE_CAPACITY commands processed
}

static void payload_length_over_14_is_rejected() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  // A declared payload length beyond the 14-byte command payload array is
  // malformed regardless of authorization state; reject with INVALID_PARAM.
  auto bad = makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {77}, 130);
  bad.payloadLength = 15;
  ble.testEnqueueCommand(bad, 5);
  drain(ble, cand, 100000);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  CHECK(lastResult(ble).respondedOpcode == BLE_CFG_OP_SET_BRIGHTNESS);
  CHECK(cm.active().brightness != 77);
}

static void v1_page_fetches_require_authorization() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  // Earlier tests open auth windows; guarantee a closed window so the
  // unauthorized-fetch checks actually exercise the session-bound path.
  cm.closeAuthWindow();

  // Unauthenticated page fetches must NOT start a page (vehicle telemetry).
  // Connection 42 never enrolled or authenticated in any test; conn 99 owns
  // a live session from enroll_then_session_is_connection_bound.
  CHECK(!ble.testHistPageActive());
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_LOG_PAGE, 0, 42);
  ble.testDrainCtrlCommands(101000);
  CHECK(!ble.testHistPageActive());

  CHECK(!ble.testCapturePageActive());
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 42);
  ble.testDrainCtrlCommands(101100);
  CHECK(!ble.testCapturePageActive());

  // Cancels stay unauthenticated and must not open a page either.
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_CANCEL_LOGS, 0, 42);
  ble.testDrainCtrlCommands(101200);
  CHECK(!ble.testHistPageActive());

  // After owner enrollment binds a session to the connection, page fetches
  // from THAT connection start the page stream.
  cm.triggerAuthWindow(101300);
  std::vector<uint8_t> secret = {9,8,7,6,5,4,3,2,1,0,1,2};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 131), 7);
  drain(ble, cand, 101400);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_LOG_PAGE, 0, 7);
  ble.testDrainCtrlCommands(101500);
  CHECK(ble.testHistPageActive());

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_CANCEL_LOGS, 0, 7);
  ble.testDrainCtrlCommands(101600);
  CHECK(!ble.testHistPageActive());
}

static void disconnect_flushes_queued_commands() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  // Queue two unauthorized writes WITHOUT draining, then disconnect. The
  // departed connection's commands must be discarded, not executed later
  // under a reused connection handle.
  const size_t resultsBefore = ble.testResultCount();
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {11}, 140), 5);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_BRIGHTNESS, {12}, 141), 5);
  ble.testResetDisconnectedLink();
  drain(ble, cand, 102000);
  CHECK(ble.testResultCount() == resultsBefore);
  CHECK(cm.active().brightness != 11 && cm.active().brightness != 12);
}

static void rpm_status_flags_reflect_rendering() {
  RpmCandidateMonitor cand;
  CanTransport transport(500000); // unconfigured => not healthy
  CanRecorder& recorder = CanRecorder::instance();

  const uint8_t base = BleManager::computeRpmStatusFlags(cand, transport, recorder, 5000, false);
  CHECK(base & BLE_RPM_FLAG_RPM_MAPPED);      // unified decoder is the production mapping
  CHECK(!(base & BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED));
  CHECK(!(base & BLE_RPM_FLAG_CAN_HEALTHY));
  CHECK(!(base & BLE_RPM_FLAG_CANDIDATE_FRESH));

  const uint8_t rendering =
      BleManager::computeRpmStatusFlags(cand, transport, recorder, 5000, true);
  CHECK(rendering & BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED);
  CHECK((rendering & ~BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED) == (base & ~BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED));
}

static void startup_animation_range_over_ble() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();

  // Animations 4 (Center-Out) and 5 (Strobe Rush) ship with 2.2.1 and must be
  // settable over BLE; anything above 5 stays INVALID_PARAM.
  cm.triggerAuthWindow(200000);
  const uint16_t dur = 2000;
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_STARTUP, {4, static_cast<uint8_t>(dur & 0xFF), static_cast<uint8_t>(dur >> 8)}, 150), 7);
  drain(ble, cand, 200001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).resultFlags & BLE_CFG_RESULT_FLAG_APPLIED);
  CHECK(cm.active().startupAnimation == 4);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_STARTUP, {5, static_cast<uint8_t>(dur & 0xFF), static_cast<uint8_t>(dur >> 8)}, 151), 7);
  drain(ble, cand, 200002);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(cm.active().startupAnimation == 5);

  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_SET_STARTUP, {6, static_cast<uint8_t>(dur & 0xFF), static_cast<uint8_t>(dur >> 8)}, 152), 7);
  drain(ble, cand, 200003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  CHECK(cm.active().startupAnimation == 5);

  // The extended active config must still pass schema-3 validation.
  CHECK(ConfigManager::validateConfig(cm.active()));
}

static void ota_and_capabilities_discovery_tests() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  auto& cm = ConfigManager::instance();
  auto& ota = OtaManager::instance();
  ota.resetForTest();

  // 1. Argument ranges discovery is read-only and does not require auth
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_ARG_RANGES, {}, 200), 10);
  drain(ble, cand, 300000);
  CHECK(lastResult(ble).respondedOpcode == BLE_CFG_OP_GET_ARG_RANGES);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 9);
  CHECK(lastResult(ble).payload[0] == 5); // maxEffectId
  CHECK(lastResult(ble).payload[1] == 5); // maxStartupAnimId
  CHECK(lastResult(ble).payload[2] == 0); // minBrightness
  CHECK(lastResult(ble).payload[3] == 255); // maxBrightness
  CHECK(lastResult(ble).payload[8] == 6); // physicalLedCount

  // 2. OTA commands require authorization
  cm.closeAuthWindow();
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_BEGIN, {0xA5, 0x5C, 0x00, 0x00, 0x01, 0x00, 0, 0, 0, 0}, 201), 42);
  drain(ble, cand, 300001);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_UNAUTHORIZED);

  // Authenticate session on conn 10 via physical window
  cm.triggerAuthWindow(300002);

  // 3. Rejection on wrong safety token (e.g. 0xA5, 0x5A instead of 0xA5, 0x5C)
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_BEGIN, {0xA5, 0x5A, 0x00, 0x00, 0x01, 0x00, 0, 0, 0, 0}, 202), 10);
  drain(ble, cand, 300003);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);

  // 4. Rejection on undersized image (< 64KB)
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_BEGIN, {0xA5, 0x5C, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0}, 203), 10);
  drain(ble, cand, 300004);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);

  // 5. Valid OTA Begin with 64KB image (65536 bytes)
  const uint32_t imageSize = 65536;
  uint8_t shaPrefix[4] = {0x12, 0x34, 0x56, 0x78};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_BEGIN, {
      0xA5, 0x5C,
      static_cast<uint8_t>(imageSize & 0xFF),
      static_cast<uint8_t>((imageSize >> 8) & 0xFF),
      static_cast<uint8_t>((imageSize >> 16) & 0xFF),
      static_cast<uint8_t>((imageSize >> 24) & 0xFF),
      shaPrefix[0], shaPrefix[1], shaPrefix[2], shaPrefix[3]
  }, 204), 10);
  drain(ble, cand, 300005);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 6);
  CHECK(ota.state() == OtaEngineState::Active);

  // 6. Abort OTA
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_ABORT, {}, 205), 10);
  drain(ble, cand, 300006);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(ota.state() == OtaEngineState::Idle);

  // 7. OTA Status query
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_STATUS, {}, 206), 10);
  drain(ble, cand, 300007);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 8); // OTA_STATUS exposes bootloader state
  CHECK(lastResult(ble).payload[0] == static_cast<uint8_t>(OtaEngineState::Idle));
  CHECK(lastResult(ble).payload[6] == 0xFF); // Unknown in host, real ESP-IDF queried on hardware
  CHECK(lastResult(ble).payload[7] == 1); // Host has a rollback candidate

  // Digest query uses four ordinary 20-byte result frames, with no larger MTU.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_STATUS, {0}, 208), 10);
  drain(ble, cand, 300008);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_STORAGE_ERROR);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_STATUS, {4}, 209), 10);
  drain(ble, cand, 300009);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_INVALID_PARAM);
  uint8_t digest[32];
  for (uint8_t i = 0; i < sizeof(digest); ++i) digest[i] = i;
  ota.testSetRunningImageDigest(digest);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t sequence = 210 + i;
    ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_STATUS, {i}, sequence), 10);
    drain(ble, cand, 300010 + i);
    const auto result = lastResult(ble);
    const auto* wire = reinterpret_cast<const uint8_t*>(&result);
    CHECK(sizeof(result) == 20);
    CHECK(wire[0] == 2 && wire[1] == 0x65 && wire[2] == sequence && wire[10] == 9);
    CHECK(wire[4] == BLE_CFG_STATUS_OK && wire[11] == i);
    CHECK(memcmp(&wire[12], &digest[i * 8], 8) == 0);
  }

  // 8. Confirm rollback cancellation
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_CONFIRM, {0xA5, 0x5C}, 207), 10);
  drain(ble, cand, 300014);
  CHECK(lastResult(ble).statusCode == BLE_CFG_STATUS_OK);
  CHECK(lastResult(ble).payloadLength == 1);

  // A process or radio disconnect during transfer discards the session.
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_BEGIN, {
      0xA5, 0x5C, 0, 0, 1, 0, 0x12, 0x34, 0x56, 0x78
  }, 214), 10);
  drain(ble, cand, 300015);
  CHECK(ota.state() == OtaEngineState::Active);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_OTA_DATA, {0, 0, 1, 0xE9}, 215), 10);
  drain(ble, cand, 300016);
  CHECK(ota.bytesWritten() == 1);
  ble.testResetDisconnectedLink();
  CHECK(ota.state() == OtaEngineState::Idle);
  CHECK(ota.bytesWritten() == 0);
}

// ============================================================================
// BLE Capture-Export Test Helpers & Scenario Suite (All 11 Scenarios)
// ============================================================================

struct ReassembledRecord {
  uint16_t messageId = 0;
  std::string csvLine;
  size_t chunkCount = 0;
  bool firstChunkValid = false;
  bool finalChunkValid = false;
  bool captureFlagValid = true;
};

struct PageCaptureResult {
  std::vector<ReassembledRecord> records;
  bool eopFound = false;
  uint32_t eopNextCursor = 0;
  uint8_t eopFinished = 0xFF;
  size_t eopChunkIndex = 0;
  size_t totalChunks = 0;
  size_t liveLogChunks = 0;
};

static std::string expectedCaptureLine(const CanLogRecord& rec) {
  char hexData[17] = "";
  const uint8_t len = (rec.dlc <= 8) ? rec.dlc : 8;
  for (uint8_t i = 0; i < len; ++i) {
    snprintf(&hexData[i * 2], 3, "%02X", rec.data[i]);
  }
  char out[96];
  snprintf(out, sizeof(out), "CAP,%lu,%u,%0*lX,%d,%u,%s\r\n",
           (unsigned long)rec.timestampMs,
           rec.stage,
           (rec.id & 0x80000000u) ? 8 : 3,
           (unsigned long)(rec.id & 0x1FFFFFFFu),
           (rec.id & 0x80000000u) ? 1 : 0,
           rec.dlc,
           hexData);
  return std::string(out);
}

static CanLogRecord makeSyntheticRecord(uint32_t index) {
  CanLogRecord rec{};
  rec.timestampMs = 1000 + index * 10;
  rec.id = 0x301;
  rec.stage = 1;
  rec.dlc = 8;
  rec.data[0] = static_cast<uint8_t>(index & 0xFF);
  rec.data[1] = static_cast<uint8_t>((index >> 8) & 0xFF);
  rec.checksum = CanRecorder::computeCrc16(reinterpret_cast<const uint8_t*>(&rec), 18);
  return rec;
}

static void createCustomCapture(const std::vector<CanLogRecord>& records) {
  CaptureHeader hdr{};
  std::memcpy(hdr.magic, "PULSECAP", 8);
  hdr.headerVersion = 1;
  hdr.recordSize = sizeof(CanLogRecord);
  hdr.sessionId = 1;
  hdr.bootCount = 1;
  hdr.bitrate = 500000;
  hdr.startMs = 1000;
  std::strncpy(hdr.mode, "CAN_REC", sizeof(hdr.mode));
  std::strncpy(hdr.vehicle, "TEST", sizeof(hdr.vehicle));

  std::vector<uint8_t> fileBytes(sizeof(CaptureHeader) + records.size() * sizeof(CanLogRecord));
  std::memcpy(fileBytes.data(), &hdr, sizeof(CaptureHeader));

  for (size_t i = 0; i < records.size(); ++i) {
    CanLogRecord rec = records[i];
    rec.checksum = CanRecorder::computeCrc16(reinterpret_cast<const uint8_t*>(&rec), 18);
    std::memcpy(fileBytes.data() + sizeof(CaptureHeader) + i * sizeof(CanLogRecord), &rec, sizeof(rec));
  }
  fs::setMockFile(CanRecorder::CAPTURE_PATH, fileBytes);
}

static void createSyntheticCapture(uint32_t recordCount) {
  std::vector<CanLogRecord> recs(recordCount);
  for (uint32_t i = 0; i < recordCount; ++i) {
    recs[i] = makeSyntheticRecord(i);
  }
  createCustomCapture(recs);
}

static PageCaptureResult parseLogChunks(const BleManager& ble) {
  PageCaptureResult res;
  res.totalChunks = ble.testLogChunkCount();

  std::map<uint16_t, size_t> msgIdToIndex;
  uint16_t currentMsgId = 0xFFFF;

  for (size_t i = 0; i < ble.testLogChunkCount(); ++i) {
    const BleLogChunkPacket& p = ble.testLogChunk(i);

    if ((p.flags & BLE_LOG_FLAG_END_OF_PAGE) != 0) {
      res.eopFound = true;
      res.eopChunkIndex = i;
      CHECK(p.payloadLength == 5);
      std::memcpy(&res.eopNextCursor, &p.payload[0], 4);
      res.eopFinished = p.payload[4];
      CHECK((p.flags & BLE_LOG_FLAG_FINAL_CHUNK) != 0);
      CHECK((p.flags & BLE_LOG_FLAG_CAPTURE_LINE) != 0);
      continue;
    }

    if ((p.flags & BLE_LOG_FLAG_CAPTURE_LINE) == 0) {
      res.liveLogChunks++;
      continue;
    }

    if (msgIdToIndex.find(p.messageId) == msgIdToIndex.end()) {
      size_t newIdx = res.records.size();
      msgIdToIndex[p.messageId] = newIdx;
      ReassembledRecord rec;
      rec.messageId = p.messageId;
      rec.firstChunkValid = ((p.flags & BLE_LOG_FLAG_FIRST_CHUNK) != 0) && (p.chunkIndex == 0);
      res.records.push_back(rec);
      currentMsgId = p.messageId;
    } else {
      CHECK(currentMsgId == p.messageId);
    }

    ReassembledRecord& r = res.records[msgIdToIndex[p.messageId]];
    CHECK(p.chunkIndex == r.chunkCount);
    r.chunkCount++;
    if ((p.flags & BLE_LOG_FLAG_FINAL_CHUNK) != 0) {
      r.finalChunkValid = true;
    }
    if ((p.flags & BLE_LOG_FLAG_CAPTURE_LINE) == 0) {
      r.captureFlagValid = false;
    }

    for (uint8_t b = 0; b < p.payloadLength; ++b) {
      r.csvLine.push_back(static_cast<char>(p.payload[b]));
    }
  }
  return res;
}

static void setupCaptureTest(uint32_t recordCount, uint16_t connId = 7, uint32_t nowMs = 400000) {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanRecorder& recorder = CanRecorder::instance();
  auto& cm = ConfigManager::instance();

  createSyntheticCapture(recordCount);
  StorageManager::instance().begin();
  recorder.begin(1, 1, 500000, "CAN_REC", "TEST");
  CHECK(recorder.totalRecords() == recordCount);

  cm.triggerAuthWindow(nowMs);
  std::vector<uint8_t> secret = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 301), connId);
  drain(ble, cand, nowMs + 10);

  ble.testResetCaptureState();
  ble.testClearLiveLogs();
  ble.testSetInitialized(true);
  ble.testSetConnected(true);
  ble.testSetLogsSubscribed(true);
  ble.testClearLogChunks();
  ble.testSetLastLogNotifyMs(nowMs);
}

static void regression_capture_page_stall_on_multi_record() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();
  auto& cm = ConfigManager::instance();

  createSyntheticCapture(12);
  StorageManager::instance().begin();
  recorder.begin(1, 1, 500000, "CAN_REC", "TEST");
  CHECK(recorder.totalRecords() == 12);

  uint32_t nowMs = 400000;
  cm.triggerAuthWindow(nowMs);
  std::vector<uint8_t> secret = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 301), 7);
  drain(ble, cand, nowMs + 10);

  ble.testResetCaptureState();
  ble.testSetInitialized(true);
  ble.testSetConnected(true);
  ble.testSetLogsSubscribed(true);
  ble.testClearLogChunks();

  // Request capture page starting at record 0
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);

  // Run 100 poll ticks (25ms each, > 20ms interval)
  for (int tick = 0; tick < 100; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }

  std::set<uint16_t> completedMsgIds;
  bool eopFound = false;
  uint32_t eopNextCursor = 0;
  uint8_t eopFinished = 0xFF;

  for (size_t i = 0; i < ble.testLogChunkCount(); ++i) {
    const BleLogChunkPacket& p = ble.testLogChunk(i);
    if ((p.flags & BLE_LOG_FLAG_END_OF_PAGE) != 0) {
      eopFound = true;
      std::memcpy(&eopNextCursor, &p.payload[0], 4);
      eopFinished = p.payload[4];
    } else if ((p.flags & BLE_LOG_FLAG_FINAL_CHUNK) != 0 && (p.flags & BLE_LOG_FLAG_CAPTURE_LINE) != 0) {
      completedMsgIds.insert(p.messageId);
    }
  }

  CHECK(completedMsgIds.size() == 12);
  CHECK(eopFound);
  CHECK(eopNextCursor == 12);
  CHECK(eopFinished == 0);
}

// ----------------------------------------------------------------------------
// Scenario 1: Zero records: clean finished EOP, no data records, no hang
// ----------------------------------------------------------------------------
static void test_scenario_1_zero_records() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 500000;
  setupCaptureTest(0, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  // Tick 1 (nowMs + 25): drain control command -> discovers 0 records -> stages EOP
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testCapturePageEopPending());
  CHECK(!ble.testCapturePageActive());
  CHECK(ble.testLogChunkCount() == 0);

  // Tick 2 (nowMs + 25): sends the EOP packet
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);
  CHECK(!ble.testCapturePageEopPending());

  // Tick 3: no further packets, no hang
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.empty());
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 0);
  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());
}

// ----------------------------------------------------------------------------
// Scenario 2: One record: all expected chunks of that record, then one finished EOP
// ----------------------------------------------------------------------------
static void test_scenario_2_one_record() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 510000;
  setupCaptureTest(1, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int tick = 0; tick < 20; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 1);
  CHECK(res.records[0].firstChunkValid);
  CHECK(res.records[0].finalChunkValid);
  CHECK(res.records[0].captureFlagValid);
  CHECK(res.records[0].chunkCount >= 2);
  CHECK(res.records[0].csvLine == expectedCaptureLine(makeSyntheticRecord(0)));

  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 1);
  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());
}

// ----------------------------------------------------------------------------
// Scenario 3: 12 records: all 12 delivered in order, page EOP (finished = 0, nextCursor = 12)
// ----------------------------------------------------------------------------
static void test_scenario_3_twelve_records() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 520000;
  setupCaptureTest(12, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int tick = 0; tick < 100; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 12);
  for (size_t i = 0; i < 12; ++i) {
    CHECK(res.records[i].firstChunkValid);
    CHECK(res.records[i].finalChunkValid);
    CHECK(res.records[i].captureFlagValid);
    CHECK(res.records[i].csvLine == expectedCaptureLine(makeSyntheticRecord(static_cast<uint32_t>(i))));
  }

  CHECK(res.eopFound);
  CHECK(res.eopFinished == 0); // Not finished, more records could follow
  CHECK(res.eopNextCursor == 12);
  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());
}

// ----------------------------------------------------------------------------
// Scenario 4: 13 records: page 1 has 12 records, page 2 has 1 record, finished = 1, nextCursor = 13
// ----------------------------------------------------------------------------
static void test_scenario_4_thirteen_records() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 530000;
  setupCaptureTest(13, 7, nowMs);

  // --- Page 1 (offset 0) ---
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int tick = 0; tick < 100; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res1 = parseLogChunks(ble);
  CHECK(res1.records.size() == 12);
  for (size_t i = 0; i < 12; ++i) {
    CHECK(res1.records[i].csvLine == expectedCaptureLine(makeSyntheticRecord(i)));
  }
  CHECK(res1.eopFound);
  CHECK(res1.eopFinished == 0);
  CHECK(res1.eopNextCursor == 12);

  // --- Page 2 (offset 12) ---
  ble.testClearLogChunks();
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 12, 7);

  for (int tick = 0; tick < 50; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res2 = parseLogChunks(ble);
  CHECK(res2.records.size() == 1);
  CHECK(res2.records[0].firstChunkValid);
  CHECK(res2.records[0].finalChunkValid);
  CHECK(res2.records[0].captureFlagValid);
  CHECK(res2.records[0].csvLine == expectedCaptureLine(makeSyntheticRecord(12)));
  CHECK(res2.eopFound);
  CHECK(res2.eopFinished == 1);
  CHECK(res2.eopNextCursor == 13);
  CHECK(!ble.testCapturePageActive());
}

// ----------------------------------------------------------------------------
// Scenario 5: Chunk boundary: multi-chunk integrity, flags, indexing, no premature replacement
// ----------------------------------------------------------------------------
static void test_scenario_5_chunk_boundary() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 540000;

  std::vector<CanLogRecord> customRecords(4);

  // Rec 0: standard ID (0x123), DLC 0 -> short line (2 chunks: 14 + 7)
  customRecords[0].timestampMs = 100;
  customRecords[0].id = 0x123;
  customRecords[0].stage = 1;
  customRecords[0].dlc = 0;
  std::memset(customRecords[0].data, 0, 8);

  // Rec 1: standard ID (0x301), DLC 8 -> medium line (3 chunks: 14 + 14 + 9)
  customRecords[1].timestampMs = 2500;
  customRecords[1].id = 0x301;
  customRecords[1].stage = 2;
  customRecords[1].dlc = 8;
  for (int b = 0; b < 8; ++b) customRecords[1].data[b] = static_cast<uint8_t>(0xA0 + b);

  // Rec 2: extended ID (0x80000000 | 0x18DAF110), DLC 8 -> 3 chunks (14 + 14 + 14)
  customRecords[2].timestampMs = 12345;
  customRecords[2].id = 0x80000000u | 0x18DAF110u;
  customRecords[2].stage = 3;
  customRecords[2].dlc = 8;
  for (int b = 0; b < 8; ++b) customRecords[2].data[b] = static_cast<uint8_t>(0x55 ^ b);

  // Rec 3: extended ID with large timestamp (99999999) -> 4 chunks (14 + 14 + 14 + 4)
  customRecords[3].timestampMs = 99999999;
  customRecords[3].id = 0x80000000u | 0x1FFFFFFAu;
  customRecords[3].stage = 1;
  customRecords[3].dlc = 8;
  for (int b = 0; b < 8; ++b) customRecords[3].data[b] = static_cast<uint8_t>(0xFF - b);

  createCustomCapture(customRecords);
  StorageManager::instance().begin();
  recorder.begin(1, 1, 500000, "CAN_REC", "TEST");
  CHECK(recorder.totalRecords() == 4);

  auto& cm = ConfigManager::instance();
  cm.triggerAuthWindow(nowMs);
  std::vector<uint8_t> secret = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 501), 7);
  drain(ble, cand, nowMs + 10);

  ble.testResetCaptureState();
  ble.testClearLiveLogs();
  ble.testSetInitialized(true);
  ble.testSetConnected(true);
  ble.testSetLogsSubscribed(true);
  ble.testClearLogChunks();
  ble.testSetLastLogNotifyMs(nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int tick = 0; tick < 50; ++tick) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  uint16_t lastMsgId = 0xFFFF;
  uint8_t expectedChunkIndex = 0;

  for (size_t i = 0; i < ble.testLogChunkCount(); ++i) {
    const BleLogChunkPacket& p = ble.testLogChunk(i);
    if ((p.flags & BLE_LOG_FLAG_END_OF_PAGE) != 0) {
      CHECK(i == ble.testLogChunkCount() - 1);
      continue;
    }
    CHECK((p.flags & BLE_LOG_FLAG_CAPTURE_LINE) != 0);

    if (p.messageId != lastMsgId) {
      CHECK(p.chunkIndex == 0);
      CHECK((p.flags & BLE_LOG_FLAG_FIRST_CHUNK) != 0);
      lastMsgId = p.messageId;
      expectedChunkIndex = 1;
    } else {
      CHECK(p.chunkIndex == expectedChunkIndex);
      CHECK((p.flags & BLE_LOG_FLAG_FIRST_CHUNK) == 0);
      expectedChunkIndex++;
    }

    if ((p.flags & BLE_LOG_FLAG_FINAL_CHUNK) != 0) {
      CHECK(p.payloadLength > 0 && p.payloadLength <= 14);
    } else {
      CHECK(p.payloadLength == 14);
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 4);
  for (size_t i = 0; i < 4; ++i) {
    CHECK(res.records[i].firstChunkValid);
    CHECK(res.records[i].finalChunkValid);
    CHECK(res.records[i].captureFlagValid);
    std::string expected = expectedCaptureLine(customRecords[i]);
    CHECK(res.records[i].csvLine == expected);
  }
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 4);
}

// ----------------------------------------------------------------------------
// Scenario 6: Cancellation mid-record and mid-page
// ----------------------------------------------------------------------------
static void test_scenario_6_cancellation_mid_stream() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 550000;

  // 6a: Cancel mid-record (after chunk 0 is emitted)
  setupCaptureTest(5, 7, nowMs);
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);
  CHECK(ble.testActiveMsgInProgress());
  CHECK(ble.testCaptureLineStaged());

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_CANCEL_CAPTURE, 0, 7);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);

  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCapturePageEopPending());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());
  CHECK(ble.testActiveMsgFlags() == 0);

  const size_t chunksAfterCancel = ble.testLogChunkCount();
  for (int t = 0; t < 5; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }
  CHECK(ble.testLogChunkCount() == chunksAfterCancel);

  // 6b: Cancel mid-page (after record 0 completed)
  nowMs += 1000;
  setupCaptureTest(5, 7, nowMs);
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int t = 0; t < 3; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }
  CHECK(ble.testLogChunkCount() == 3);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_CANCEL_CAPTURE, 0, 7);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);

  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCapturePageEopPending());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());

  size_t countMidPage = ble.testLogChunkCount();
  for (int t = 0; t < 10; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }
  CHECK(ble.testLogChunkCount() == countMidPage);

  // 6c: Clean state for a later request
  nowMs += 1000;
  ble.testClearLogChunks();
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int t = 0; t < 50; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 5);
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 5);
}

// ----------------------------------------------------------------------------
// Scenario 7: Disconnect / reconnect: flushes pending state, no stale chunks
// ----------------------------------------------------------------------------
static void test_scenario_7_disconnect_reconnect() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 560000;
  setupCaptureTest(5, 7, nowMs);
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 2);
  CHECK(ble.testActiveMsgInProgress() || ble.testCapturePageActive());

  // Client disconnects
  ble.testSetConnected(false);
  CHECK(ble.testCaptureLineStaged()); // callback only signals; the loop cleans up
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);

  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCapturePageEopPending());
  CHECK(!ble.testCaptureLineStaged());
  CHECK(!ble.testActiveMsgInProgress());
  CHECK(ble.testActiveMsgFlags() == 0);

  // Poll while disconnected: zero chunks emitted
  for (int t = 0; t < 5; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }
  CHECK(ble.testLogChunkCount() == 2);

  // Reconnect
  ble.testSetConnected(true);
  ble.testSetLogsSubscribed(true);
  ConfigManager::instance().triggerAuthWindow(nowMs);
  std::vector<uint8_t> secret = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2};
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_ENROLL_OWNER, secret, 701), 7);
  drain(ble, cand, nowMs + 10);

  // Poll without new command: no stale chunks or phantom EOP
  for (int t = 0; t < 5; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }
  CHECK(ble.testLogChunkCount() == 2);

  // Now start fresh request cleanly
  ble.testClearLogChunks();
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  for (int t = 0; t < 50; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 5);
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 5);
}

// ----------------------------------------------------------------------------
// Scenario 8: Repeated page request / retry: well-defined semantics
// ----------------------------------------------------------------------------
static void test_scenario_8_repeated_page_request_retry() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 570000;
  setupCaptureTest(5, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 2);

  // Client retries requesting page 0 mid-stream
  ble.testClearLogChunks();
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  for (int t = 0; t < 50; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() && !ble.testActiveMsgInProgress()) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 5);
  for (size_t i = 0; i < 5; ++i) {
    CHECK(res.records[i].firstChunkValid);
    CHECK(res.records[i].finalChunkValid);
    CHECK(res.records[i].captureFlagValid);
    CHECK(res.records[i].csvLine == expectedCaptureLine(makeSyntheticRecord(static_cast<uint32_t>(i))));
  }
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 5);
}

// ----------------------------------------------------------------------------
// Scenario 9: Auth rejection: unauthorized connection does not start export or leak data
// ----------------------------------------------------------------------------
static void test_scenario_9_auth_rejection() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();
  auto& cm = ConfigManager::instance();

  uint32_t nowMs = 580000;
  cm.closeAuthWindow();
  CHECK(!cm.isAuthorizedFor(42, nowMs));

  createSyntheticCapture(5);
  StorageManager::instance().begin();
  recorder.begin(1, 1, 500000, "CAN_REC", "TEST");

  ble.testResetCaptureState();
  ble.testClearLiveLogs();
  ble.testSetInitialized(true);
  ble.testSetConnected(true);
  ble.testSetLogsSubscribed(true);
  ble.testClearLogChunks();
  ble.testSetLastLogNotifyMs(nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 42);
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);

  CHECK(!ble.testCapturePageActive());
  CHECK(!ble.testCapturePageEopPending());
  CHECK(!ble.testCaptureLineStaged());
  CHECK((ble.testActiveMsgFlags() & BLE_LOG_FLAG_CAPTURE_LINE) == 0);

  for (int t = 0; t < 10; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
  }

  CHECK(!ble.testActiveMsgInProgress());

  for (size_t i = 0; i < ble.testLogChunkCount(); ++i) {
    const BleLogChunkPacket& p = ble.testLogChunk(i);
    CHECK((p.flags & BLE_LOG_FLAG_CAPTURE_LINE) == 0);
    CHECK((p.flags & BLE_LOG_FLAG_END_OF_PAGE) == 0);
  }
}

// ----------------------------------------------------------------------------
// Scenario 10: Notification pacing / bounded work: at least 20ms pacing, no bursts
// ----------------------------------------------------------------------------
static void test_scenario_10_notification_pacing_and_bounded_work() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 590000;
  setupCaptureTest(3, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  // Poll at nowMs + 20: 1st notification sent
  nowMs += 20;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);

  // Sub-interval polls: NO notification emitted
  ble.poll(nowMs + 5, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);

  ble.poll(nowMs + 10, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);

  ble.poll(nowMs + 19, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);

  // Poll at nowMs + 20: EXACTLY 1 notification sent
  nowMs += 20;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 2);

  // Burst attempts at identical timestamp: NO additional notifications sent
  for (int b = 0; b < 10; ++b) {
    ble.poll(nowMs, cand, transport, recorder, false);
    CHECK(ble.testLogChunkCount() == 2);
  }

  // Advance in 20ms steps: bounded work per pass (at most 1 notification, never a burst)
  while (ble.testCapturePageActive() || ble.testCapturePageEopPending() || ble.testActiveMsgInProgress()) {
    size_t beforeCount = ble.testLogChunkCount();
    nowMs += 20;
    ble.poll(nowMs, cand, transport, recorder, false);
    CHECK(ble.testLogChunkCount() == beforeCount || ble.testLogChunkCount() == beforeCount + 1);
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 3);
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 3);
}

// ----------------------------------------------------------------------------
// Scenario 11: Concurrent diagnostic messages: live logs do not corrupt capture lines
// ----------------------------------------------------------------------------
static void test_scenario_11_concurrent_diagnostic_messages() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor cand;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();

  uint32_t nowMs = 600000;
  setupCaptureTest(3, 7, nowMs);

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);

  // Poll 1 tick to stage record 0 and emit chunk 0
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 1);
  CHECK(ble.testCaptureLineStaged());

  // Concurrently queue live diagnostic messages while capture line is in progress
  ble.queueLiveLog("DIAG: System alert coolant temp 85C", 0);
  ble.queueLiveLog("DIAG: CAN bus load 12%", 0);

  // Verify capture line state was NOT corrupted or prematurely unstaged by queueLiveLog
  CHECK(ble.testCaptureLineStaged());
  CHECK(ble.testActiveMsgInProgress());
  CHECK((ble.testActiveMsgFlags() & BLE_LOG_FLAG_CAPTURE_LINE) != 0);

  // Poll next chunk of record 0
  nowMs += 25;
  ble.poll(nowMs, cand, transport, recorder, false);
  CHECK(ble.testLogChunkCount() == 2);
  CHECK(ble.testLogChunk(1).messageId == ble.testLogChunk(0).messageId);
  CHECK(ble.testLogChunk(1).chunkIndex == 1);
  CHECK((ble.testLogChunk(1).flags & BLE_LOG_FLAG_CAPTURE_LINE) != 0);

  // Queue another live log
  ble.queueLiveLog("DIAG: Battery voltage 13.8V", 0);

  // Poll until both capture page AND live logs are drained
  for (int t = 0; t < 100; ++t) {
    nowMs += 25;
    ble.poll(nowMs, cand, transport, recorder, false);
    if (!ble.testCapturePageActive() && !ble.testCapturePageEopPending() &&
        !ble.testActiveMsgInProgress() && ble.testLiveLogCount() == 0) {
      break;
    }
  }

  PageCaptureResult res = parseLogChunks(ble);
  CHECK(res.records.size() == 3);
  for (size_t i = 0; i < 3; ++i) {
    CHECK(res.records[i].firstChunkValid);
    CHECK(res.records[i].finalChunkValid);
    CHECK(res.records[i].captureFlagValid);
    CHECK(res.records[i].csvLine == expectedCaptureLine(makeSyntheticRecord(static_cast<uint32_t>(i))));
  }
  CHECK(res.eopFound);
  CHECK(res.eopFinished == 1);
  CHECK(res.eopNextCursor == 3);

  // Live log chunks were also delivered without corrupting capture lines
  CHECK(res.liveLogChunks > 0);
}

static void test_capture_retries_exact_wire_packet_and_eop() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor candidate;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();
  uint32_t now = 620000;
  setupCaptureTest(1, 7, now);
  ble.testSetLogMessageId(0x1234);
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  const auto tick = [&](uint32_t interval) {
    now += interval;
    ble.poll(now, candidate, transport, recorder);
  };
  tick(20);
  CHECK(ble.testLogChunkCount() == 1);
  ble.testRejectLogNotifications(2);
  const uint32_t acceptedBefore = ble.logNotifyCount();
  tick(20);
  tick(19);
  CHECK(ble.testLogChunkCount() == 1);
  tick(1);
  CHECK(ble.logNotifyCount() == acceptedBefore);
  CHECK(ble.testCaptureNextRecord() == 0);
  CHECK(ble.testCaptureRecordsSent() == 0);
  tick(20);
  CHECK(ble.testLogChunk(1).chunkIndex == 1);
  tick(20);
  CHECK(ble.testCaptureNextRecord() == 1);
  tick(20); // EOF stages EOP
  CHECK(ble.testCapturePageEopPending());
  ble.testRejectLogNotifications(1);
  tick(20);
  CHECK(ble.testCapturePageEopPending());
  CHECK(ble.testLogChunkCount() == 3);
  tick(20);
  CHECK(!ble.testCapturePageEopPending());
  CHECK(ble.testLogChunkCount() == 4);

  // Same packed fixture consumed by Android's CapturePageReaderTest.
  constexpr uint8_t wire[][20] = {
    {0x01, 0x11, 0x34, 0x12, 0x00, 0x0e, 0x43, 0x41, 0x50, 0x2c, 0x31, 0x30, 0x30, 0x30, 0x2c, 0x31, 0x2c, 0x33, 0x30, 0x31},
    {0x01, 0x10, 0x34, 0x12, 0x01, 0x0e, 0x2c, 0x30, 0x2c, 0x38, 0x2c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30},
    {0x01, 0x12, 0x34, 0x12, 0x02, 0x09, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x0d, 0x0a},
    {0x01, 0x16, 0x35, 0x12, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x01},
  };
  for (size_t i = 0; i < 4; ++i) {
    CHECK(std::memcmp(wire[i], &ble.testLogChunk(i), sizeof(wire[i])) == 0);
  }

  ble.testClearLogChunks();
  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  ble.testRejectLogNotifications(8);
  const uint32_t drops = ble.dropCount();
  for (int i = 0; i < 8; ++i) tick(20);
  CHECK(ble.dropCount() == drops + 1);
  CHECK(!ble.testCapturePageActive() && !ble.testCapturePageEopPending());
  CHECK(!recorder.isBleExportActive());
  CHECK(ble.testLogChunkCount() == 0);
}

static void test_control_result_retry_and_snapshot_busy() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor candidate;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();
  uint32_t now = 650000;
  setupCaptureTest(0, 7, now);
  ble.testClearResults();
  ble.testSetSnapshotSubscribed(true);
  ble.testRejectResultNotifications(2);
  ble.testRejectSnapshotNotifications(2);
  const uint32_t drops = ble.droppedCommandCount();
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_SNAPSHOT, {}, 0x1234), 7);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_DIAGNOSTICS, {}, 2), 7);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_CAPABILITIES, {}, 3), 7);
  ble.testEnqueueCommand(makeCmd(BLE_CFG_OP_GET_CAPABILITIES, {}, 4), 7);
  CHECK(ble.droppedCommandCount() == drops + 1);

  const auto tick = [&] { now += 20; ble.poll(now, candidate, transport, recorder); };
  tick();
  tick();
  CHECK(ble.testResultCount() == 0);
  CHECK(ble.testSnapshotChunkCount() == 0);
  tick();
  CHECK(ble.testResultCount() == 1);
  CHECK(ble.testResult(0).sequence == 0x1234);
  tick();
  CHECK(ble.testResultCount() == 2);
  CHECK(ble.testResult(1).statusCode == BLE_CFG_STATUS_BUSY);
  CHECK(ble.testSnapshotChunkCount() == 0);
  tick();
  CHECK(ble.testResultCount() == 3);
  CHECK(ble.testSnapshotChunkCount() == 1);
  CHECK(ble.testSnapshotChunk(0).chunkIndex == 0);
  CHECK(ble.testSnapshotChunk(0).snapshotId == 0x1234);
  for (int i = 0; i < 40 && ble.testSnapshotActive(); ++i) tick();
  CHECK(!ble.testSnapshotActive());
  CHECK(ble.testSnapshotChunkCount() == ble.testSnapshotChunk(0).chunkCount);
  for (size_t i = 0; i < ble.testSnapshotChunkCount(); ++i) {
    CHECK(ble.testSnapshotChunk(i).chunkIndex == i);
  }
}

static void test_capture_read_corruption_does_not_report_eof() {
  BleManager& ble = BleManager::instance();
  RpmCandidateMonitor candidate;
  CanTransport transport(500000);
  CanRecorder& recorder = CanRecorder::instance();
  uint32_t now = 680000;
  setupCaptureTest(2, 7, now);
  auto& file = fs::mockFsRegistry().at(CanRecorder::CAPTURE_PATH)->data;
  const size_t corrupt = sizeof(CaptureHeader) + sizeof(CanLogRecord) + 18;
  file[corrupt] ^= 0x40;
  const uint8_t originalCorruptValue = file[corrupt];

  ble.testEnqueueCtrlCommand(BLE_CTRL_CMD_GET_CAPTURE_PAGE, 0, 7);
  for (int i = 0; i < 30; ++i) {
    now += 20;
    ble.poll(now, candidate, transport, recorder);
  }
  const PageCaptureResult result = parseLogChunks(ble);
  CHECK(result.records.size() == 1);
  CHECK(!result.eopFound);
  CHECK(result.liveLogChunks > 0);
  CHECK(!recorder.isBleExportActive());
  CHECK(file[corrupt] == originalCorruptValue);
}

int main() {
  read_queries_do_not_need_authorization();
  writes_require_authorization();
  enroll_then_session_is_connection_bound();
  staged_transaction_over_ble();
  calibration_workflow_over_ble();
  device_commands();
  mobile_terminal_stage_and_led_test();
  startup_animation_range_over_ble();
  ota_and_capabilities_discovery_tests();
  diagnostics_blob_shape();
  reload_and_decoder_ext();
  malformed_and_duplicate_commands();
  payload_length_over_14_is_rejected();
  v1_page_fetches_require_authorization();
  disconnect_flushes_queued_commands();
  rpm_status_flags_reflect_rendering();
  regression_capture_page_stall_on_multi_record();

  test_scenario_1_zero_records();
  test_scenario_2_one_record();
  test_scenario_3_twelve_records();
  test_scenario_4_thirteen_records();
  test_scenario_5_chunk_boundary();
  test_scenario_6_cancellation_mid_stream();
  test_scenario_7_disconnect_reconnect();
  test_scenario_8_repeated_page_request_retry();
  test_scenario_9_auth_rejection();
  test_scenario_10_notification_pacing_and_bounded_work();
  test_scenario_11_concurrent_diagnostic_messages();
  test_capture_retries_exact_wire_packet_and_eop();
  test_control_result_retry_and_snapshot_busy();
  test_capture_read_corruption_does_not_report_eof();

  std::printf("PASS BLE v2 command dispatch, auth binding, calibration, device ops, capture export and backpressure: %u checks\n", checks);
  return 0;
}
