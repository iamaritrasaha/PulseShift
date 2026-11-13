// BLE v2 command dispatch tests (host build with test hooks):
// authorization binding, staged transactions, calibration workflow and the
// device control commands, all through the real BleManager dispatcher.
#include "ble_manager.h"
#include "config_manager.h"
#include "rpm_candidate.h"
#include "can_transport.h"
#include "can_recorder.h"
#include "storage_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

int main() {
  read_queries_do_not_need_authorization();
  writes_require_authorization();
  enroll_then_session_is_connection_bound();
  staged_transaction_over_ble();
  calibration_workflow_over_ble();
  device_commands();
  diagnostics_blob_shape();
  reload_and_decoder_ext();
  malformed_and_duplicate_commands();
  std::printf("PASS BLE v2 command dispatch, auth binding, calibration, device ops: %u checks\n", checks);
  return 0;
}
