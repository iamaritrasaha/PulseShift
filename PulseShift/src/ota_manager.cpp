// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "ota_manager.h"
#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

// ============================================================================
// Portable FIPS 180-2 / RFC 6234 SHA-256 Engine
// Self-contained to guarantee deterministic behavior across ESP32 and host tests
// ============================================================================
namespace {
inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sig0(uint32_t x) {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sig1(uint32_t x) {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gam0(uint32_t x) {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gam1(uint32_t x) {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

const uint32_t K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
  uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
  for (int i = 0, j = 0; i < 16; ++i, j += 4) {
    m[i] = (static_cast<uint32_t>(data[j]) << 24) |
           (static_cast<uint32_t>(data[j + 1]) << 16) |
           (static_cast<uint32_t>(data[j + 2]) << 8) |
           (static_cast<uint32_t>(data[j + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    m[i] = gam1(m[i - 2]) + m[i - 7] + gam0(m[i - 15]) + m[i - 16];
  }
  a = state[0]; b = state[1]; c = state[2]; d = state[3];
  e = state[4]; f = state[5]; g = state[6]; h = state[7];
  for (int i = 0; i < 64; ++i) {
    t1 = h + sig1(e) + ch(e, f, g) + K[i] + m[i];
    t2 = sig0(a) + maj(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

uint32_t compute_crc32(uint32_t crc, const uint8_t* buf, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *buf++;
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320u & -(static_cast<int32_t>(crc & 1)));
    }
  }
  return ~crc;
}
} // namespace

OtaManager::OtaManager() {
  resetForTest();
}

void OtaManager::resetForTest() {
  state_ = OtaEngineState::Idle;
  totalSize_ = 0;
  bytesWritten_ = 0;
  expectedChunkIndex_ = 0;
  sessionToken_ = 0;
  targetSlot_ = 0;
  rebootTimeMs_ = 0;
  memset(sha256Prefix_, 0, sizeof(sha256Prefix_));
  initSha256();
#if defined(ARDUINO)
  otaHandle_ = 0;
#endif
}

void OtaManager::begin() {
  resetForTest();
}

uint8_t OtaManager::activeRunningSlot() const {
#if defined(ARDUINO)
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running && strcmp(running->label, "app1") == 0) {
    return 1;
  }
  return 0;
#else
  return 0; // Default slot 0 (app0) in host tests
#endif
}

void OtaManager::initSha256() {
  shaState_[0] = 0x6a09e667; shaState_[1] = 0xbb67ae85;
  shaState_[2] = 0x3c6ef372; shaState_[3] = 0xa54ff53a;
  shaState_[4] = 0x510e527f; shaState_[5] = 0x9b05688c;
  shaState_[6] = 0x1f83d9ab; shaState_[7] = 0x5be0cd19;
  shaCount_[0] = 0;
  shaCount_[1] = 0;
  memset(shaBuffer_, 0, sizeof(shaBuffer_));
}

void OtaManager::updateSha256(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    shaBuffer_[shaCount_[0] % 64] = data[i];
    shaCount_[0]++;
    if (shaCount_[0] == 0) shaCount_[1]++;
    if ((shaCount_[0] % 64) == 0) {
      sha256_transform(shaState_, shaBuffer_);
    }
  }
}

void OtaManager::finishSha256(uint8_t output[32]) {
  const uint32_t bitLow = shaCount_[0] << 3;
  const uint32_t bitHigh = (shaCount_[1] << 3) | (shaCount_[0] >> 29);

  uint8_t pad = 0x80;
  updateSha256(&pad, 1);
  while ((shaCount_[0] % 64) != 56) {
    uint8_t zero = 0x00;
    updateSha256(&zero, 1);
  }
  uint8_t lenBytes[8];
  for (int i = 0; i < 4; ++i) {
    lenBytes[i] = static_cast<uint8_t>((bitHigh >> (24 - i * 8)) & 0xFF);
    lenBytes[i + 4] = static_cast<uint8_t>((bitLow >> (24 - i * 8)) & 0xFF);
  }
  updateSha256(lenBytes, 8);

  for (int i = 0; i < 8; ++i) {
    output[i * 4]     = static_cast<uint8_t>((shaState_[i] >> 24) & 0xFF);
    output[i * 4 + 1] = static_cast<uint8_t>((shaState_[i] >> 16) & 0xFF);
    output[i * 4 + 2] = static_cast<uint8_t>((shaState_[i] >> 8) & 0xFF);
    output[i * 4 + 3] = static_cast<uint8_t>(shaState_[i] & 0xFF);
  }
}

BleConfigStatusCode OtaManager::startOta(uint32_t totalSize, const uint8_t sha256_prefix[4],
                                        const RpmCandidateMonitor& rpmMon, uint32_t nowMs,
                                        uint32_t& outSessionToken, uint8_t& outTargetSlot) {
  // Safety Interlock 1: Vehicle must be stationary and bench-safe
  if (rpmMon.isFresh(nowMs) && rpmMon.estimatedRpm(nowMs) > 0) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }

  // Safety Interlock 2: Validate image size against partition limit (1,310,720 bytes)
  constexpr uint32_t MIN_OTA_SIZE = 64 * 1024;      // 64 KB minimum valid ESP32 application
  constexpr uint32_t MAX_OTA_SIZE = 0x140000;       // 1.25 MB partition limit
  if (totalSize < MIN_OTA_SIZE || totalSize > MAX_OTA_SIZE) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }

  // Identify inactive partition target slot
#if defined(ARDUINO)
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
  if (!running || !updatePart || updatePart == running) {
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }
  targetSlot_ = (strcmp(updatePart->label, "app1") == 0) ? 1 : 0;

  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(updatePart, totalSize, &handle);
  if (err != ESP_OK) {
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }
  otaHandle_ = handle;
#else
  targetSlot_ = 1; // In host test, target slot 1
#endif

  totalSize_ = totalSize;
  bytesWritten_ = 0;
  expectedChunkIndex_ = 0;
  memcpy(sha256Prefix_, sha256_prefix, 4);

  // Generate pseudo-random session token based on time and size
  sessionToken_ = (nowMs ^ (totalSize << 7) ^ 0xA55C0000u);
  if (sessionToken_ == 0) sessionToken_ = 0x12345678;

  initSha256();
  state_ = OtaEngineState::Active;

  outSessionToken = sessionToken_;
  outTargetSlot = targetSlot_;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode OtaManager::writeChunk(uint16_t chunkIndex, const uint8_t* data, size_t len,
                                          uint32_t& outBytesWritten) {
  if (state_ != OtaEngineState::Active) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  if (chunkIndex != expectedChunkIndex_) {
    // Sequence mismatch
    return BLE_CFG_STATUS_INVALID_PARAM;
  }
  if (bytesWritten_ + len > totalSize_) {
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  // Inspect image header magic on first chunk
  if (bytesWritten_ == 0) {
    if (len < 1 || data[0] != 0xE7) {
      // Not a valid ESP32 image (ESP_IMAGE_HEADER_MAGIC = 0xE7)
      abortOta();
      return BLE_CFG_STATUS_VALIDATION_FAILED;
    }
  }

#if defined(ARDUINO)
  esp_err_t err = esp_ota_write(static_cast<esp_ota_handle_t>(otaHandle_), data, len);
  if (err != ESP_OK) {
    abortOta();
    return BLE_CFG_STATUS_OTA_FLASH_ERROR;
  }
#endif

  updateSha256(data, len);
  bytesWritten_ += static_cast<uint32_t>(len);
  expectedChunkIndex_++;

  outBytesWritten = bytesWritten_;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode OtaManager::endOta(const uint8_t sha256_tail[4], uint32_t /*crc32*/,
                                      uint8_t& outNextSlot, uint32_t& outRebootDelayMs) {
  if (state_ != OtaEngineState::Active) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  if (bytesWritten_ != totalSize_) {
    abortOta();
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  uint8_t computedSha[32];
  finishSha256(computedSha);

  // Validate SHA-256 prefix and tail
  if (memcmp(computedSha, sha256Prefix_, 4) != 0 ||
      memcmp(computedSha + 28, sha256_tail, 4) != 0) {
    abortOta();
    return BLE_CFG_STATUS_OTA_CHECKSUM_ERROR;
  }

#if defined(ARDUINO)
  esp_err_t err = esp_ota_end(static_cast<esp_ota_handle_t>(otaHandle_));
  if (err != ESP_OK) {
    abortOta();
    return BLE_CFG_STATUS_OTA_FLASH_ERROR;
  }
  const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
  if (!updatePart || esp_ota_set_boot_partition(updatePart) != ESP_OK) {
    abortOta();
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }
#endif

  state_ = OtaEngineState::RebootPending;
  outNextSlot = targetSlot_;
  outRebootDelayMs = 1500; // 1.5s delay to allow BLE ACK delivery before restart
  return BLE_CFG_STATUS_OK;
}

void OtaManager::abortOta() {
  if (state_ == OtaEngineState::Active) {
#if defined(ARDUINO)
    if (otaHandle_ != 0) {
      esp_ota_abort(static_cast<esp_ota_handle_t>(otaHandle_));
      otaHandle_ = 0;
    }
#endif
  }
  state_ = OtaEngineState::Idle;
  bytesWritten_ = 0;
  totalSize_ = 0;
  expectedChunkIndex_ = 0;
}

BleConfigStatusCode OtaManager::confirmRollbackCancel(uint8_t& outActiveSlot) {
#if defined(ARDUINO)
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }
#endif
  outActiveSlot = activeRunningSlot();
  return BLE_CFG_STATUS_OK;
}

bool OtaManager::verifyAndCommitBootSlot() {
#if defined(ARDUINO)
  esp_ota_img_states_t ota_state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      return true; // Pending self-test verification
    }
  }
#endif
  return false;
}
