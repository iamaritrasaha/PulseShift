// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ble_protocol.h"
#include "rpm_candidate.h"

enum class OtaEngineState : uint8_t {
  Idle = 0,
  Active = 1,
  Verifying = 2,
  RebootPending = 3
};

class OtaManager {
public:
  static OtaManager& instance() {
    static OtaManager inst;
    return inst;
  }

  void begin();

  // Pre-flight validation & session start
  BleConfigStatusCode startOta(uint32_t totalSize, const uint8_t sha256_prefix[4],
                               const RpmCandidateMonitor& rpmMon, uint32_t nowMs,
                               uint32_t& outSessionToken, uint8_t& outTargetSlot);

  // Chunk processing
  BleConfigStatusCode writeChunk(uint16_t chunkIndex, const uint8_t* data, size_t len,
                                 uint32_t& outBytesWritten);

  // Finalize & commit
  BleConfigStatusCode endOta(const uint8_t sha256_tail[4], uint32_t crc32,
                             uint8_t& outNextSlot, uint32_t& outRebootDelayMs);

  // Abort
  void abortOta();

  // Cancel rollback upon successful post-reboot verification
  BleConfigStatusCode confirmRollbackCancel(uint8_t& outActiveSlot);

  // State inspection
  OtaEngineState state() const { return state_; }
  uint8_t activeRunningSlot() const;
  uint32_t bytesWritten() const { return bytesWritten_; }
  uint32_t totalExpectedSize() const { return totalSize_; }
  bool isRebootPending() const { return state_ == OtaEngineState::RebootPending; }
  uint32_t rebootScheduledTimeMs() const { return rebootTimeMs_; }

  // Self-test verification on boot
  static bool verifyAndCommitBootSlot();

  // Test helpers
  void resetForTest();

private:
  OtaManager();
  ~OtaManager() = default;
  OtaManager(const OtaManager&) = delete;
  OtaManager& operator=(const OtaManager&) = delete;

  OtaEngineState state_ = OtaEngineState::Idle;
  uint32_t totalSize_ = 0;
  uint32_t bytesWritten_ = 0;
  uint16_t expectedChunkIndex_ = 0;
  uint32_t sessionToken_ = 0;
  uint8_t targetSlot_ = 0;
  uint8_t sha256Prefix_[4] = {0};
  uint32_t rebootTimeMs_ = 0;

  // Running SHA-256 calculation
  uint32_t shaState_[8] = {0};
  uint32_t shaCount_[2] = {0};
  uint8_t  shaBuffer_[64] = {0};
  void initSha256();
  void updateSha256(const uint8_t* data, size_t len);
  void finishSha256(uint8_t output[32]);

#if defined(ARDUINO)
  uint32_t otaHandle_ = 0;
#endif
};
