// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "ota_manager.h"
#include <string.h>
#include <stdio.h>

#if defined(ARDUINO)
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#elif defined(PULSESHIFT_USE_MBEDTLS)
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include "esp_ota_ops.h"
#include "esp_partition.h"
#else
#include "esp_ota_ops.h"
#include "esp_partition.h"
#endif

#ifndef ESP_IMAGE_HEADER_MAGIC
#define ESP_IMAGE_HEADER_MAGIC 0xE9
#endif

// IEEE 802.3 CRC32 implementation
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320u;
      } else {
        crc = (crc >> 1);
      }
    }
  }
  return ~crc;
}

// SHA-256 primitives (FIPS 180-2 compliant pure C++ fallback)
static inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t ep0(uint32_t x) {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t ep1(uint32_t x) {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t sig0(uint32_t x) {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t sig1(uint32_t x) {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static const uint32_t kK[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
  uint32_t m[64];
  for (int i = 0, j = 0; i < 16; ++i, j += 4) {
    m[i] = (static_cast<uint32_t>(data[j]) << 24) |
           (static_cast<uint32_t>(data[j + 1]) << 16) |
           (static_cast<uint32_t>(data[j + 2]) << 8) |
           (static_cast<uint32_t>(data[j + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

  for (int i = 0; i < 64; ++i) {
    uint32_t t1 = h + ep1(e) + ch(e, f, g) + kK[i] + m[i];
    uint32_t t2 = ep0(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// Cryptographic signature verification using mbedtls (ESP32/Linux) or Windows CNG
static bool verifyEcdsaP256Signature(const uint8_t* pubKeySec1, size_t pubKeyLen,
                                     const uint8_t* sha256Digest,
                                     const uint8_t* rawSig, size_t sigLen) {
  if (pubKeyLen != 65 || sigLen != 64) return false;
  if (pubKeySec1[0] != 0x04) return false;

#if defined(ARDUINO) || defined(PULSESHIFT_USE_MBEDTLS)
  mbedtls_ecp_group grp;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point Q;
  mbedtls_ecp_point_init(&Q);
  mbedtls_mpi r, s;
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  bool ok = false;
  if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0) {
    if (mbedtls_ecp_point_read_binary(&grp, &Q, pubKeySec1, pubKeyLen) == 0) {
      if (mbedtls_mpi_read_binary(&r, rawSig, 32) == 0 &&
          mbedtls_mpi_read_binary(&s, rawSig + 32, 32) == 0) {
        if (mbedtls_ecdsa_verify(&grp, sha256Digest, 32, &Q, &r, &s) == 0) {
          ok = true;
        }
      }
    }
  }

  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&s);
  mbedtls_ecp_point_free(&Q);
  mbedtls_ecp_group_free(&grp);
  return ok;
#elif defined(_WIN32)
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  bool ok = false;

  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) == 0) {
    struct {
      BCRYPT_ECCKEY_BLOB hdr;
      uint8_t x[32];
      uint8_t y[32];
    } blob;
    blob.hdr.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.hdr.cbKey = 32;
    memcpy(blob.x, pubKeySec1 + 1, 32);
    memcpy(blob.y, pubKeySec1 + 33, 32);

    if (BCryptImportKeyPair(hAlg, NULL, BCRYPT_ECCPUBLIC_BLOB, &hKey,
                            reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0) == 0) {
      NTSTATUS status = BCryptVerifySignature(hKey, NULL,
                                              const_cast<PUCHAR>(sha256Digest), 32,
                                              const_cast<PUCHAR>(rawSig), 64, 0);
      if (status == 0) {
        ok = true;
      }
      BCryptDestroyKey(hKey);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
  }
  return ok;
#else
  #error "No cryptographic ECDSA verifier available for this platform"
#endif
}

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
  lastTransferActivityMs_ = 0;
  rebootStartedMs_ = 0;
  rebootDelayMs_ = 0;
  runningImageDigestValid_ = false;
  memset(sha256Prefix_, 0, sizeof(sha256Prefix_));
  memset(trailerBuffer_, 0, sizeof(trailerBuffer_));
  trailerBytesBuffered_ = 0;
  rollbackConfirmed_ = false;
  bootSelfTestPassed_ = false;
  initCrc32();
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
  if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
    return 1;
  }
  return 0;
#else
  return 0; // Default slot 0 (app0) in host tests
#endif
}

bool OtaManager::isRollbackPossible() const {
#if defined(ARDUINO)
  return esp_ota_check_rollback_is_possible();
#else
  return true; // Supported in host test environment
#endif
}

bool OtaManager::isDualSlotSupported() const {
#if defined(ARDUINO)
  const esp_partition_t* p0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  const esp_partition_t* p1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  const esp_partition_t* otaData = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  return (p0 != nullptr && p1 != nullptr && otaData != nullptr);
#else
  return true; // Supported in host test environment
#endif
}

bool OtaManager::readRunningImageDigest(uint8_t out[32]) {
  if (!runningImageDigestValid_) {
#if defined(ARDUINO)
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running || esp_partition_get_sha256(running, runningImageDigest_) != ESP_OK) {
      return false;
    }
    runningImageDigestValid_ = true;
#else
    return false;
#endif
  }
  memcpy(out, runningImageDigest_, sizeof(runningImageDigest_));
  return true;
}

#if defined(PULSESHIFT_TEST_BUILD)
void OtaManager::testSetRunningImageDigest(const uint8_t digest[32]) {
  memcpy(runningImageDigest_, digest, sizeof(runningImageDigest_));
  runningImageDigestValid_ = true;
}
#endif

void OtaManager::initCrc32() {
  runningCrc32_ = 0;
}

void OtaManager::updateCrc32(const uint8_t* data, size_t len) {
  runningCrc32_ = crc32_update(runningCrc32_, data, len);
}

uint32_t OtaManager::finalizeCrc32() const {
  return runningCrc32_;
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

namespace {
bool otaStationary(const RpmCandidateMonitor& rpmMon, const CanTransport& transport, uint32_t nowMs) {
#if defined(ARDUINO)
  if (!transport.healthy()) return false;
#endif
  return !(rpmMon.isFresh(nowMs) && rpmMon.estimatedRpm(nowMs) > 0) &&
         !(transport.totalRx() > 0 && uint32_t(nowMs - transport.lastRxMs()) < 3000);
}
}

BleConfigStatusCode OtaManager::serviceTransfer(const RpmCandidateMonitor& rpmMon,
                                                const CanTransport& transport, uint32_t nowMs) {
  if (state_ == OtaEngineState::Active &&
      (!otaStationary(rpmMon, transport, nowMs) ||
       uint32_t(nowMs - lastTransferActivityMs_) >= 15000)) {
    abortOta();
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  return BLE_CFG_STATUS_OK;
}

void OtaManager::scheduleReboot(uint32_t nowMs, uint32_t delayMs) {
  rebootStartedMs_ = nowMs;
  rebootDelayMs_ = delayMs;
}

bool OtaManager::rebootDue(uint32_t nowMs) const {
  return state_ == OtaEngineState::RebootPending && rebootDelayMs_ != 0 &&
         uint32_t(nowMs - rebootStartedMs_) >= rebootDelayMs_;
}

BleConfigStatusCode OtaManager::startOta(uint32_t totalSize, const uint8_t sha256_prefix[4],
                                        const RpmCandidateMonitor& rpmMon,
                                        const CanTransport& transport,
                                        uint32_t nowMs,
                                        uint32_t& outSessionToken, uint8_t& outTargetSlot) {
  if (state_ != OtaEngineState::Idle) {
    return BLE_CFG_STATUS_BUSY;
  }

  // Safety Interlock 0: Partition layout must support dual-slot OTA
  if (!isDualSlotSupported()) {
    return BLE_CFG_STATUS_UNSUPPORTED;
  }

  // Require zero live RPM and three seconds without observed CAN frames.
  if (!otaStationary(rpmMon, transport, nowMs)) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }

  // Safety Interlock 3: Validate image size against partition limit (1,310,720 bytes)
  constexpr uint32_t MIN_OTA_SIZE = 64 * 1024;
  constexpr uint32_t MAX_OTA_SIZE = 0x140000; // 1.25 MB partition limit
  if (totalSize < MIN_OTA_SIZE || totalSize > MAX_OTA_SIZE) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }

  // Identify inactive partition target slot
#if defined(ARDUINO)
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
  if (!running || !updatePart || updatePart->address == running->address ||
      (running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
       running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) ||
      (updatePart->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
       updatePart->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }
  // Keep a known-valid running app available as the first rollback target.
  esp_ota_img_states_t runningState = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (!boot || boot->address != running->address ||
      esp_ota_get_state_partition(running, &runningState) != ESP_OK ||
      runningState != ESP_OTA_IMG_VALID) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  targetSlot_ = updatePart->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? 1 : 0;

  esp_ota_handle_t handle = 0;
  // Begin OTA targeting the application binary size (excluding trailer)
  const uint32_t appBinarySize = totalSize - OTA_TRAILER_SIZE;
  if (appBinarySize > updatePart->size) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }
  esp_err_t err = esp_ota_begin(updatePart, appBinarySize, &handle);
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
  trailerBytesBuffered_ = 0;
  memset(trailerBuffer_, 0, sizeof(trailerBuffer_));
  memcpy(sha256Prefix_, sha256_prefix, 4);

  // Generate pseudo-random session token based on time and size
  sessionToken_ = (nowMs ^ (totalSize << 7) ^ 0xA55C0000u);
  if (sessionToken_ == 0) sessionToken_ = 0x12345678;

  initCrc32();
  initSha256();
  lastTransferActivityMs_ = nowMs;
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

  // Support seamless 16-bit sequence wraparound across large (>65,535) chunk streams
  if (static_cast<uint16_t>(expectedChunkIndex_ & 0xFFFF) != chunkIndex) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }
  if (bytesWritten_ + len > totalSize_) {
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  // Inspect image header magic on first chunk (Espressif ESP_IMAGE_HEADER_MAGIC = 0xE9)
  if (bytesWritten_ == 0) {
    if (len < 1 || data[0] != ESP_IMAGE_HEADER_MAGIC) {
      abortOta();
      return BLE_CFG_STATUS_VALIDATION_FAILED;
    }
  }

  const uint32_t appBinarySize = totalSize_ - OTA_TRAILER_SIZE;

  // Segment payload into application binary vs authentication trailer
  if (bytesWritten_ < appBinarySize) {
    const size_t appBytes = (bytesWritten_ + len <= appBinarySize)
                              ? len
                              : (appBinarySize - bytesWritten_);
    updateCrc32(data, appBytes);
    updateSha256(data, appBytes);

#if defined(ARDUINO)
    esp_err_t err = esp_ota_write(static_cast<esp_ota_handle_t>(otaHandle_), data, appBytes);
    if (err != ESP_OK) {
      abortOta();
      return BLE_CFG_STATUS_OTA_FLASH_ERROR;
    }
#endif

    // Buffer any remainder into the authentication trailer
    if (appBytes < len) {
      const size_t trailerBytes = len - appBytes;
      if (trailerBytesBuffered_ + trailerBytes <= OTA_TRAILER_SIZE) {
        memcpy(trailerBuffer_ + trailerBytesBuffered_, data + appBytes, trailerBytes);
        trailerBytesBuffered_ += trailerBytes;
      }
    }
  } else {
    // Entire chunk belongs to the authentication trailer
    if (trailerBytesBuffered_ + len <= OTA_TRAILER_SIZE) {
      memcpy(trailerBuffer_ + trailerBytesBuffered_, data, len);
      trailerBytesBuffered_ += len;
    }
  }

  bytesWritten_ += static_cast<uint32_t>(len);
  expectedChunkIndex_++;

  outBytesWritten = bytesWritten_;
  return BLE_CFG_STATUS_OK;
}

void OtaManager::computeSha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  OtaManager temp;
  temp.initSha256();
  temp.updateSha256(data, len);
  temp.finishSha256(out);
}

uint32_t OtaManager::computeCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0;
  return crc32_update(crc, data, len);
}

BleConfigStatusCode OtaManager::endOta(const uint8_t sha256_tail[4], uint32_t expectedCrc32,
                                      uint8_t& outNextSlot, uint32_t& outRebootDelayMs) {
  if (state_ != OtaEngineState::Active) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }

  // Complete transfer check: must match total expected bytes exactly (no truncated images)
  if (bytesWritten_ != totalSize_ || trailerBytesBuffered_ != OTA_TRAILER_SIZE) {
    abortOta();
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  // Step 1: Complete application binary CRC32 check
  const uint32_t calculatedCrc = finalizeCrc32();
  if (calculatedCrc != expectedCrc32) {
    abortOta();
    return BLE_CFG_STATUS_OTA_CHECKSUM_ERROR;
  }

  // Step 2: Validate SHA-256 running digest
  uint8_t computedSha[32];
  finishSha256(computedSha);

  if (memcmp(computedSha, sha256Prefix_, 4) != 0 ||
      memcmp(computedSha + 28, sha256_tail, 4) != 0) {
    abortOta();
    return BLE_CFG_STATUS_OTA_CHECKSUM_ERROR;
  }

  // Step 3: Parse and validate authentication trailer
  const auto* trailer = reinterpret_cast<const OtaAuthTrailer*>(trailerBuffer_);
  if (memcmp(trailer->magic, "PSOT", 4) != 0 || trailer->version != 1 ||
      trailer->flags != 0 || trailer->sigLen != 64) {
    abortOta();
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  if (trailer->appCrc32 != calculatedCrc) {
    abortOta();
    return BLE_CFG_STATUS_OTA_CHECKSUM_ERROR;
  }

  // Step 4: Trusted digital signature verification (NIST P-256 ECDSA over full SHA-256)
  if (!verifyEcdsaP256Signature(PULSESHIFT_OTA_PUBLIC_KEY, sizeof(PULSESHIFT_OTA_PUBLIC_KEY),
                                computedSha, trailer->signature, 64)) {
    abortOta();
    return BLE_CFG_STATUS_OTA_CHECKSUM_ERROR;
  }

#if defined(ARDUINO)
  // Step 5: Finalize flash write and commit inactive slot
  esp_err_t err = esp_ota_end(static_cast<esp_ota_handle_t>(otaHandle_));
  otaHandle_ = 0;
  if (err != ESP_OK) {
    abortOta();
    return BLE_CFG_STATUS_OTA_FLASH_ERROR;
  }

  const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
  if (!updatePart) {
    abortOta();
    return BLE_CFG_STATUS_STORAGE_ERROR;
  }

  // Step 6: Verify image segments on partition before setting boot slot
  const esp_partition_pos_t partPos = { updatePart->address, updatePart->size };
  esp_image_metadata_t imgData{};
  if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &partPos, &imgData) != ESP_OK ||
      imgData.image_len != totalSize_ - OTA_TRAILER_SIZE) {
    abortOta();
    return BLE_CFG_STATUS_VALIDATION_FAILED;
  }

  if (esp_ota_set_boot_partition(updatePart) != ESP_OK) {
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
  if (state_ != OtaEngineState::Active) return;
#if defined(ARDUINO)
  if (otaHandle_ != 0) {
    esp_ota_abort(static_cast<esp_ota_handle_t>(otaHandle_));
    otaHandle_ = 0;
  }
#endif
  state_ = OtaEngineState::Idle;
  bytesWritten_ = 0;
  totalSize_ = 0;
  expectedChunkIndex_ = 0;
  trailerBytesBuffered_ = 0;
  memset(trailerBuffer_, 0, sizeof(trailerBuffer_));
}

BleConfigStatusCode OtaManager::confirmRollbackCancel(uint8_t& outActiveSlot) {
#if defined(ARDUINO)
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
  if (!running || esp_ota_get_state_partition(running, &otaState) != ESP_OK) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  // Idempotent if confirmation succeeded but the BLE ACK was lost.
  // UNDEFINED is NOT success: it can indicate a rollback-disabled bootloader.
  if (otaState != ESP_OTA_IMG_PENDING_VERIFY && otaState != ESP_OTA_IMG_VALID) {
    return BLE_CFG_STATUS_INVALID_STATE;
  }
  if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    if (!bootSelfTestPassed_ || !isRollbackPossible()) {
      return BLE_CFG_STATUS_INVALID_STATE;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
      return BLE_CFG_STATUS_STORAGE_ERROR;
    }
  }
#endif
  rollbackConfirmed_ = true;
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
