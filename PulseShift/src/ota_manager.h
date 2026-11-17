// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ble_protocol.h"
#include "rpm_candidate.h"
#include "can_transport.h"
#include "ota_public_key.h"

enum class OtaEngineState : uint8_t {
  Idle = 0,
  Active = 1,
  Verifying = 2,
  RebootPending = 3
};

// PulseShift Authenticated OTA Trailer (76 bytes appended to firmware binary)
// Wire format: Little-endian fields with packed alignment
// [0..3]:   Magic "PSOT" (0x50, 0x53, 0x4F, 0x54)
// [4]:      Version (1)
// [5]:      Flags (0, reserved)
// [6..9]:   appCrc32 (Little-endian IEEE 802.3 CRC32 of application binary)
// [10..11]: sigLen (Little-endian uint16_t, exactly 64)
// [12..75]: signature (Raw NIST P-256 ECDSA r[32] || s[32], big-endian as standard)
#if defined(__GNUC__) || defined(__clang__)
#define PULSESHIFT_PACKED __attribute__((packed))
#else
#define PULSESHIFT_PACKED
#endif

#pragma pack(push, 1)
struct OtaAuthTrailer {
  uint8_t  magic[4];      // 0..3: "PSOT"
  uint8_t  version;       // 4: 1
  uint8_t  flags;         // 5: 0 (reserved)
  uint32_t appCrc32;      // 6..9: Little-endian CRC32 of application binary
  uint16_t sigLen;        // 10..11: 64 bytes
  uint8_t  signature[64]; // 12..75: Raw r[32] || s[32]
} PULSESHIFT_PACKED;
#pragma pack(pop)

static_assert(sizeof(OtaAuthTrailer) == 76, "OtaAuthTrailer must be exactly 76 bytes");
static_assert(offsetof(OtaAuthTrailer, magic) == 0, "magic offset must be 0");
static_assert(offsetof(OtaAuthTrailer, version) == 4, "version offset must be 4");
static_assert(offsetof(OtaAuthTrailer, flags) == 5, "flags offset must be 5");
static_assert(offsetof(OtaAuthTrailer, appCrc32) == 6, "appCrc32 offset must be 6");
static_assert(offsetof(OtaAuthTrailer, sigLen) == 10, "sigLen offset must be 10");
static_assert(offsetof(OtaAuthTrailer, signature) == 12, "signature offset must be 12");

static constexpr size_t OTA_TRAILER_SIZE = sizeof(OtaAuthTrailer); // exactly 76 bytes

class OtaManager {
public:
  static OtaManager& instance() {
    static OtaManager inst;
    return inst;
  }

  void begin();

  // Pre-flight validation & session start with full safety interlocks
  BleConfigStatusCode startOta(uint32_t totalSize, const uint8_t sha256_prefix[4],
                               const RpmCandidateMonitor& rpmMon,
                               const CanTransport& transport,
                               uint32_t nowMs,
                               uint32_t& outSessionToken, uint8_t& outTargetSlot);

  // Chunk processing with 16-bit wraparound support and progressive CRC32/SHA-256
  BleConfigStatusCode writeChunk(uint16_t chunkIndex, const uint8_t* data, size_t len,
                                 uint32_t& outBytesWritten);

  // Finalize & commit: validates CRC32, SHA-256, ECDSA signature, and partition integrity
  BleConfigStatusCode endOta(const uint8_t sha256_tail[4], uint32_t expectedCrc32,
                             uint8_t& outNextSlot, uint32_t& outRebootDelayMs);

  // Abort active session
  void abortOta();

  // Cancel rollback upon successful post-reboot verification
  void recordBootSelfTestPassed() { bootSelfTestPassed_ = true; }
  BleConfigStatusCode confirmRollbackCancel(uint8_t& outActiveSlot);

  // Query whether rollback is possible from hardware bootloader state
  bool isRollbackPossible() const;
  bool isDualSlotSupported() const;
  bool readRunningImageDigest(uint8_t out[32]);
  bool rollbackConfirmed() const { return rollbackConfirmed_; }

  // State inspection
  BleConfigStatusCode serviceTransfer(const RpmCandidateMonitor& rpmMon,
                                      const CanTransport& transport, uint32_t nowMs);
  void noteChunkAcknowledged(uint32_t nowMs) { lastTransferActivityMs_ = nowMs; }
  void scheduleReboot(uint32_t nowMs, uint32_t delayMs);
  bool rebootDue(uint32_t nowMs) const;
  OtaEngineState state() const { return state_; }
  uint8_t activeRunningSlot() const;
  uint32_t bytesWritten() const { return bytesWritten_; }
  uint32_t totalExpectedSize() const { return totalSize_; }
  bool isRebootPending() const { return state_ == OtaEngineState::RebootPending; }

  // Self-test verification on boot
  static bool verifyAndCommitBootSlot();

  // Public cryptographic helpers
  static void computeSha256(const uint8_t* data, size_t len, uint8_t out[32]);
  static uint32_t computeCrc32(const uint8_t* data, size_t len);

  // Test helpers
  void resetForTest();
#if defined(PULSESHIFT_TEST_BUILD)
  void testSetRunningImageDigest(const uint8_t digest[32]);
#endif

private:
  OtaManager();
  ~OtaManager() = default;
  OtaManager(const OtaManager&) = delete;
  OtaManager& operator=(const OtaManager&) = delete;

  OtaEngineState state_ = OtaEngineState::Idle;
  uint32_t totalSize_ = 0;
  uint32_t bytesWritten_ = 0;
  uint32_t expectedChunkIndex_ = 0;
  uint32_t sessionToken_ = 0;
  uint8_t targetSlot_ = 0;
  uint8_t sha256Prefix_[4] = {0};
  uint32_t lastTransferActivityMs_ = 0;
  uint32_t rebootStartedMs_ = 0;
  uint32_t rebootDelayMs_ = 0;
  bool runningImageDigestValid_ = false;
  uint8_t runningImageDigest_[32] = {};

  // Running CRC32 calculation
  uint32_t runningCrc32_ = 0xFFFFFFFF;
  void initCrc32();
  void updateCrc32(const uint8_t* data, size_t len);
  uint32_t finalizeCrc32() const;

  // Running SHA-256 calculation
  uint32_t shaState_[8] = {0};
  uint32_t shaCount_[2] = {0};
  uint8_t  shaBuffer_[64] = {0};
  void initSha256();
  void updateSha256(const uint8_t* data, size_t len);
  void finishSha256(uint8_t output[32]);

  // Trailer buffering
  uint8_t trailerBuffer_[OTA_TRAILER_SIZE] = {0};
  size_t  trailerBytesBuffered_ = 0;
  bool rollbackConfirmed_ = false;
  bool bootSelfTestPassed_ = false;

#if defined(ARDUINO)
  uint32_t otaHandle_ = 0;
#endif
};
