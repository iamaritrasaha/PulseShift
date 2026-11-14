// Copyright (c) 2026 Aritra Saha. All rights reserved.
// Automated Fault-Injection and Cryptographic Verification Tests for PulseShift OTA

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "ota_manager.h"
#include "rpm_candidate.h"
#include "can_transport.h"

// Stubs for host testing of TWAI / GPIO calls
static bool s_mockTwaiRx = false;
int gpio_reset_pin(int) { return 0; }
int gpio_set_direction(int, int) { return 0; }
int gpio_set_pull_mode(int, int) { return 0; }
int twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*) { return 0; }
int twai_start() { return 0; }
int twai_stop() { return 0; }
int twai_initiate_recovery() { return 0; }
int twai_read_alerts(uint32_t*, uint32_t) { return 0; }
int twai_get_status_info(twai_status_info_t* s) {
  if (s) s->state = TWAI_STATE_RUNNING;
  return 0;
}
int twai_receive(twai_message_t* m, uint32_t) {
  if (s_mockTwaiRx && m) {
    m->identifier = 0x123;
    m->data_length_code = 8;
    return 0; // ESP_OK
  }
  return 0x107; // ESP_ERR_TIMEOUT
}

// Valid raw ECDSA P-256 signature (r || s, 64 bytes) computed for the 65,536-byte deterministic
// test payload (byte 0 = 0xE9, byte i = (i*7 + 13) & 0xFF) using the private key corresponding
// to PULSESHIFT_OTA_PUBLIC_KEY in ota_public_key.h.
static const uint8_t VALID_TEST_SIG[64] = {
  0x5C, 0xC6, 0xE2, 0x75, 0x51, 0x6B, 0xE9, 0xD3,
  0xC2, 0xA4, 0xAA, 0x3A, 0x78, 0xF1, 0xB3, 0x52,
  0x4F, 0xB3, 0x66, 0xAF, 0x1D, 0xE7, 0x16, 0xD6,
  0xF9, 0xBF, 0x19, 0x05, 0x54, 0xC7, 0x4C, 0x21,
  0xF2, 0xCE, 0x12, 0x2A, 0xB2, 0x28, 0x34, 0x0F,
  0xDC, 0xD7, 0x39, 0x3B, 0xE5, 0x69, 0x66, 0xA0,
  0x5F, 0x96, 0x2D, 0x5B, 0xAE, 0x8E, 0x8F, 0xC0,
  0x2A, 0x78, 0xB4, 0xBF, 0xD8, 0xBA, 0x57, 0xAA
};

// Helper to construct a synthetic test firmware image with 76-byte PSOT trailer
static void buildTestImage(std::vector<uint8_t>& outImage, size_t payloadSize,
                           uint8_t sha256Prefix[4], uint8_t sha256Tail[4], uint32_t& outCrc32,
                           const uint8_t* rawSignature = VALID_TEST_SIG,
                           const char* magic = "PSOT", uint8_t version = 1,
                           uint8_t flags = 0, uint16_t sigLen = 64) {
  outImage.resize(payloadSize + OTA_TRAILER_SIZE);
  // Byte 0: valid ESP32 image magic 0xE9 (ESP_IMAGE_HEADER_MAGIC)
  outImage[0] = 0xE9;
  for (size_t i = 1; i < payloadSize; ++i) {
    outImage[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
  }

  // Compute CRC32 over application payload
  outCrc32 = OtaManager::computeCrc32(outImage.data(), payloadSize);

  // Compute actual SHA-256 over application payload
  uint8_t fullSha[32];
  OtaManager::computeSha256(outImage.data(), payloadSize, fullSha);
  memcpy(sha256Prefix, fullSha, 4);
  memcpy(sha256Tail, fullSha + 28, 4);

  // Build explicitly packed 76-byte PSOT trailer
  OtaAuthTrailer trailer{};
  memcpy(trailer.magic, magic, 4);
  trailer.version = version;
  trailer.flags = flags;
  trailer.appCrc32 = outCrc32;
  trailer.sigLen = sigLen;
  if (rawSignature) {
    memcpy(trailer.signature, rawSignature, (sigLen <= 64) ? sigLen : 64);
  }

  memcpy(outImage.data() + payloadSize, &trailer, sizeof(trailer));
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("Starting PulseShift OTA Cryptographic & Fault-Injection Tests...\n");

  RpmCandidateMonitor rpmMon;
  CanTransport transport(500000);
  const uint32_t nowMs = 1000;

  // --- Test 1: Vehicle Running Safety Interlock (RPM > 0) ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    // Simulate active engine running at 3000 RPM
    CanFrame rpmFrame{};
    rpmFrame.id = 0x301;
    rpmFrame.dlc = 8;
    rpmFrame.data[0] = 75; // 75 * 40 = 3000 RPM
    rpmMon.ingest(rpmFrame, nowMs);
    assert(rpmMon.isFresh(nowMs));
    assert(rpmMon.estimatedRpm(nowMs) == 3000);

    uint8_t prefix[4] = {0x12, 0x34, 0x56, 0x78};
    uint32_t token = 0;
    uint8_t targetSlot = 0;
    const auto res = ota.startOta(70000, prefix, rpmMon, transport, nowMs, token, targetSlot);
    assert(res == BLE_CFG_STATUS_INVALID_STATE);
    printf("PASS 1: OTA rejected while motorcycle engine is operating (RPM > 0)\n");
  }

  // --- Test 2: Active CAN Traffic Safety Interlock ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor idleMon; // Stale / zero RPM
    assert(!idleMon.isFresh(nowMs));

    // Simulate active CAN bus traffic on transport
    transport.begin();
    s_mockTwaiRx = true;
    CanFrame dummyFrame{};
    bool got = transport.receive(dummyFrame, nowMs); // Registers RX at nowMs
    assert(got);
    s_mockTwaiRx = false;
    assert(transport.hasTraffic(nowMs));

    uint8_t prefix[4] = {0x12, 0x34, 0x56, 0x78};
    uint32_t token = 0;
    uint8_t targetSlot = 0;
    const auto res = ota.startOta(70000, prefix, idleMon, transport, nowMs, token, targetSlot);
    assert(res == BLE_CFG_STATUS_INVALID_STATE);
    printf("PASS 2: OTA rejected while CAN bus is actively receiving frames\n");
  }

  // --- Test 3: Valid Cryptographically Signed OTA Transfer and Commit ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000); // 0 RX frames, totally idle bench

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    constexpr size_t PAYLOAD_SIZE = 65536; // 64 KB app
    buildTestImage(img, PAYLOAD_SIZE, prefix, tail, crc32, VALID_TEST_SIG);

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    auto status = ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                              nowMs, token, targetSlot);
    assert(status == BLE_CFG_STATUS_OK);
    assert(ota.state() == OtaEngineState::Active);
    assert(targetSlot == 1);

    // Stream image in 128-byte chunks (efficient MTU)
    constexpr size_t CHUNK_SIZE = 128;
    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += CHUNK_SIZE) {
      const size_t len = (offset + CHUNK_SIZE <= img.size()) ? CHUNK_SIZE : (img.size() - offset);
      uint32_t written = 0;
      status = ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
      assert(status == BLE_CFG_STATUS_OK);
      assert(written == offset + len);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    status = ota.endOta(tail, crc32, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_OK);
    assert(nextSlot == 1);
    assert(ota.isRebootPending());
    printf("PASS 3: Complete authenticated image transfer and commit succeeded (real ECDSA P-256)\n");
  }

  // --- Test 4: Fault Injection - Corrupted CRC32 Rejection ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, VALID_TEST_SIG);

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += 128) {
      const size_t len = (offset + 128 <= img.size()) ? 128 : (img.size() - offset);
      uint32_t written = 0;
      ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    // Pass corrupted CRC32 (mismatched)
    const auto status = ota.endOta(tail, crc32 ^ 0xFFFFFFFF, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_OTA_CHECKSUM_ERROR);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 4: Image with mismatched CRC32 rejected and aborted\n");
  }

  // --- Test 5: Fault Injection - Corrupted ECDSA Signature Rejection ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    uint8_t corruptedSig[64];
    memcpy(corruptedSig, VALID_TEST_SIG, 64);
    corruptedSig[10] ^= 0xFF; // Corrupt a signature byte

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, corruptedSig);

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += 128) {
      const size_t len = (offset + 128 <= img.size()) ? 128 : (img.size() - offset);
      uint32_t written = 0;
      ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    const auto status = ota.endOta(tail, crc32, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_OTA_CHECKSUM_ERROR);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 5: Image with corrupted ECDSA signature rejected\n");
  }

  // --- Test 6: Fault Injection - Tampered Payload Rejection (Valid Trailer Sig for Original) ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, VALID_TEST_SIG);

    // Tamper with payload byte 100
    img[100] ^= 0x55;
    // Recompute CRC32 so that CRC matches tampered payload, but signature will fail
    const uint32_t tamperedCrc = OtaManager::computeCrc32(img.data(), 65536);
    // Update trailer CRC field
    OtaAuthTrailer* tr = reinterpret_cast<OtaAuthTrailer*>(img.data() + 65536);
    tr->appCrc32 = tamperedCrc;

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += 128) {
      const size_t len = (offset + 128 <= img.size()) ? 128 : (img.size() - offset);
      uint32_t written = 0;
      ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    const auto status = ota.endOta(tail, tamperedCrc, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_OTA_CHECKSUM_ERROR);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 6: Tampered payload with valid CRC rejected due to cryptographic signature mismatch\n");
  }

  // --- Test 7: Fault Injection - Malformed Trailer Magic Rejection ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, VALID_TEST_SIG, "BAD!");

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += 128) {
      const size_t len = (offset + 128 <= img.size()) ? 128 : (img.size() - offset);
      uint32_t written = 0;
      ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    const auto status = ota.endOta(tail, crc32, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_VALIDATION_FAILED);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 7: Trailer with invalid magic 'BAD!' rejected\n");
  }

  // --- Test 8: Fault Injection - Unsupported Trailer Version Rejection ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, VALID_TEST_SIG, "PSOT", 99); // version 99

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    uint16_t chunkIdx = 0;
    for (size_t offset = 0; offset < img.size(); offset += 128) {
      const size_t len = (offset + 128 <= img.size()) ? 128 : (img.size() - offset);
      uint32_t written = 0;
      ota.writeChunk(chunkIdx++, img.data() + offset, len, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    const auto status = ota.endOta(tail, crc32, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_VALIDATION_FAILED);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 8: Trailer with unsupported version 99 rejected\n");
  }

  // --- Test 9: Fault Injection - Truncated Transfer Rejection ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    std::vector<uint8_t> img;
    uint8_t prefix[4], tail[4];
    uint32_t crc32 = 0;
    buildTestImage(img, 65536, prefix, tail, crc32, VALID_TEST_SIG);

    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(static_cast<uint32_t>(img.size()), prefix, benchRpm, benchTransport,
                 nowMs, token, targetSlot);

    // Write only half the image, simulating power loss or connection drop
    for (size_t offset = 0; offset < img.size() / 2; offset += 128) {
      uint32_t written = 0;
      ota.writeChunk(static_cast<uint16_t>(offset / 128), img.data() + offset, 128, written);
    }

    uint8_t nextSlot = 0;
    uint32_t delayMs = 0;
    const auto status = ota.endOta(tail, crc32, nextSlot, delayMs);
    assert(status == BLE_CFG_STATUS_VALIDATION_FAILED);
    assert(ota.state() == OtaEngineState::Idle);
    printf("PASS 9: Truncated transfer rejected and aborted\n");
  }

  // --- Test 10: Fault Injection - Invalid ESP32 Header Magic & Regression ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    uint8_t prefix[4] = {0x00};
    uint32_t token = 0;
    uint8_t targetSlot = 0;
    ota.startOta(70000, prefix, benchRpm, benchTransport, nowMs, token, targetSlot);

    uint8_t badData[16] = {0x00, 0x11, 0x22}; // First byte NOT 0xE9
    uint32_t written = 0;
    auto status = ota.writeChunk(0, badData, sizeof(badData), written);
    assert(status == BLE_CFG_STATUS_VALIDATION_FAILED);
    assert(ota.state() == OtaEngineState::Idle);

    // Regression: Ensure old erroneous magic 0xE7 is strictly rejected
    ota.resetForTest();
    ota.startOta(70000, prefix, benchRpm, benchTransport, nowMs, token, targetSlot);
    uint8_t oldMagicData[16] = {0xE7, 0x11, 0x22}; // 0xE7 must be rejected
    status = ota.writeChunk(0, oldMagicData, sizeof(oldMagicData), written);
    assert(status == BLE_CFG_STATUS_VALIDATION_FAILED);
    assert(ota.state() == OtaEngineState::Idle);

    printf("PASS 10: Non-ESP32 binaries (including 0xE7 regression) rejected on chunk 0\n");
  }

  // --- Test 11: Sequence Wraparound Handling (>65,535 Chunks) ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    RpmCandidateMonitor benchRpm;
    CanTransport benchTransport(500000);

    // Simulate stream of 65,540 chunks with 16-bit wraparound
    uint8_t prefix[4] = {0x12, 0x34, 0x56, 0x78};
    uint32_t token = 0;
    uint8_t targetSlot = 0;
    // Set total size large enough for 65540 chunks of 11 bytes + 76
    const uint32_t totalBytes = (65540 * 11) + static_cast<uint32_t>(OTA_TRAILER_SIZE);
    ota.startOta(totalBytes, prefix, benchRpm, benchTransport, nowMs, token, targetSlot);

    uint8_t dummy[11] = {0xE9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    for (uint32_t i = 0; i < 65540; ++i) {
      if (i > 0) dummy[0] = 0x55;
      const uint16_t wireIdx = static_cast<uint16_t>(i & 0xFFFF);
      uint32_t written = 0;
      const auto status = ota.writeChunk(wireIdx, dummy, 11, written);
      assert(status == BLE_CFG_STATUS_OK);
    }
    printf("PASS 11: 16-bit sequence wraparound (>65,535 chunks) verified successfully\n");
  }

  // --- Test 12: Rollback Confirmation Policy ---
  {
    auto& ota = OtaManager::instance();
    ota.resetForTest();

    assert(!ota.rollbackConfirmed());
    uint8_t slot = 0;
    const auto status = ota.confirmRollbackCancel(slot);
    assert(status == BLE_CFG_STATUS_OK);
    assert(ota.rollbackConfirmed());
    printf("PASS 12: Operational rollback confirmation policy verified\n");
  }

  printf("\nALL 12 OTA CRYPTOGRAPHIC & FAULT-INJECTION TESTS PASSED.\n");
  return 0;
}
