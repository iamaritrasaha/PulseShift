// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "config_manager.h"
#include <string.h>
#include <math.h>

#if defined(ARDUINO)
#include <Arduino.h>
#if !defined(PULSESHIFT_TEST_BUILD)
#include <Preferences.h>
#include <mbedtls/sha256.h>
#endif
#elif defined(PULSESHIFT_TEST_BUILD)
#include <Arduino.h>
#else
#include <chrono>
static uint32_t millis() {
  using namespace std::chrono;
  static auto start = steady_clock::now();
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}
#endif

namespace {
// Dedicated NVS preferences namespace
constexpr const char* NVS_NAMESPACE = "ps_cfg";
constexpr const char* NVS_KEY_BLOB = "blob";
constexpr uint32_t NVS_MAGIC = 0x50534332; // "PSC2" in ASCII (PulseShift Config v2)

constexpr const char* NVS_SEC_NAMESPACE = "ps_sec";
constexpr const char* NVS_KEY_CREDS = "creds";
constexpr uint32_t NVS_SEC_MAGIC = 0x50534543; // "PSEC" in ASCII (PulseShift Security)

#pragma pack(push, 1)
struct NvsConfigBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  PulseShiftConfig cfg;
  uint32_t crc32;
};

struct OwnerSecurityBlob {
  uint32_t magic;
  uint8_t  isEnrolled;
  uint8_t  ownerSecret[12];
  uint32_t enrollTimestamp;
  uint32_t crc32;
};
#pragma pack(pop)
} // namespace

ConfigManager::ConfigManager() {
  applySafeDefaults(active_);
}

ConfigManager& ConfigManager::instance() {
  static ConfigManager inst;
  return inst;
}

void ConfigManager::applySafeDefaults(PulseShiftConfig& cfg) {
  cfg.schemaVersion = 2;
  cfg.activeRevision = 1;
  cfg.isPowerOn = true;
  cfg.brightness = 200;
  cfg.globalColor = RgbColor{140, 43, 238}; // BrandViolet default
  for (uint8_t i = 0; i < 6; ++i) {
    cfg.ledColors[i] = RgbColor{140, 43, 238};
  }
  cfg.effect = 0; // Static
  cfg.effectSpeed = 50;
  cfg.ledDirection = 0; // Normal
  cfg.startupAnimation = 1; // Sweep
  cfg.startupDurationMs = 2000;
  cfg.rpmThresholds[0] = 3000;
  cfg.rpmThresholds[1] = 4000;
  cfg.rpmThresholds[2] = 5000;
  cfg.rpmThresholds[3] = 6000;
  cfg.rpmThresholds[4] = 7000;
  cfg.rpmThresholds[5] = 8000;
  cfg.warningRpm = 8500;
  cfg.warningPatternId = 0; // Flash
  cfg.warningColor = RgbColor{255, 0, 0};
  cfg.rpmMultiplier = 40.0f;
  cfg.canCandidateId = 0x301;
  cfg.candidateByteIndex = 0;
  cfg.isExtendedFrame = false;
  cfg.calibrationVerified = false; // Must remain strictly unverified until validated
}

uint32_t ConfigManager::computeCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; ++j) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFFFFFF;
}

bool ConfigManager::validateConfig(const PulseShiftConfig& cfg) {
  if (cfg.schemaVersion != 2) return false;
  if (cfg.effect > 4) return false;
  // Safety rule: RPM Bar (effect 4) requires verified production calibration
  if (cfg.effect == 4 && !cfg.calibrationVerified) return false;
  if (cfg.effectSpeed > 100) return false;
  if (cfg.ledDirection > 1) return false;
  if (cfg.startupAnimation > 3) return false;
  if (cfg.startupDurationMs < 100 || cfg.startupDurationMs > 10000) return false;

  // Thresholds validation: Monotonic ascending and within 0..20,000 RPM
  for (uint8_t i = 0; i < 6; ++i) {
    if (cfg.rpmThresholds[i] > 20000) return false;
    if (i > 0 && cfg.rpmThresholds[i] < cfg.rpmThresholds[i - 1]) return false;
  }
  if (cfg.warningRpm < cfg.rpmThresholds[5] || cfg.warningRpm > 20000) return false;
  if (cfg.warningPatternId > 3) return false;

  // Multiplier validation: 0.1 .. 1000.0, reject NaN / inf
  if (isnan(cfg.rpmMultiplier) || isinf(cfg.rpmMultiplier) ||
      cfg.rpmMultiplier < 0.1f || cfg.rpmMultiplier > 1000.0f) {
    return false;
  }

  // CAN Candidate validation
  if (cfg.candidateByteIndex >= 8) return false;
  if (cfg.isExtendedFrame) {
    if (cfg.canCandidateId > 0x1FFFFFFF) return false;
  } else {
    if (cfg.canCandidateId > 0x7FF) return false;
  }

  return true;
}

bool ConfigManager::begin() {
#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) { // Read-only mount
    NvsConfigBlob blob{};
    const size_t len = prefs.getBytes(NVS_KEY_BLOB, &blob, sizeof(blob));
    prefs.end();

    if (len == sizeof(blob) && blob.magic == NVS_MAGIC && blob.version == 2) {
      const uint32_t calcCrc = computeCrc32(reinterpret_cast<const uint8_t*>(&blob),
                                            sizeof(blob) - sizeof(uint32_t));
      if (calcCrc == blob.crc32 && validateConfig(blob.cfg)) {
        active_ = blob.cfg;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.printf("[CONFIG] Loaded persistent settings from NVS (revision=%lu)\r\n",
                      (unsigned long)active_.activeRevision);
#endif
        return true;
      }
    }
  }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[CONFIG] NVS settings missing or corrupt; applied documented safe defaults.");
#endif
#endif

  applySafeDefaults(active_);

#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  // Load owner security credentials from dedicated NVS namespace
  Preferences secPrefs;
  if (secPrefs.begin(NVS_SEC_NAMESPACE, true)) { // Read-only mount
    OwnerSecurityBlob secBlob{};
    const size_t secLen = secPrefs.getBytes(NVS_KEY_CREDS, &secBlob, sizeof(secBlob));
    secPrefs.end();

    if (secLen == sizeof(secBlob) && secBlob.magic == NVS_SEC_MAGIC && secBlob.isEnrolled == 1) {
      const uint32_t calcCrc = computeCrc32(reinterpret_cast<const uint8_t*>(&secBlob),
                                            sizeof(secBlob) - sizeof(uint32_t));
      if (calcCrc == secBlob.crc32) {
        ownerEnrolled_ = true;
        memcpy(ownerSecret_, secBlob.ownerSecret, OWNER_SECRET_LEN);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.println("[SECURITY] Owner credential loaded from NVS.");
#endif
      }
    }
  }
#endif

  return true;
}

void ConfigManager::poll(uint32_t nowMs) {
  // Check transaction timeout
  if (stagedTxActive_ && uint32_t(nowMs - stagedTxStartMs_) >= TX_TIMEOUT_MS) {
    stagedTxActive_ = false;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[CONFIG] Staged transaction timed out; discarded.");
#endif
  }

  // Check active challenge timeout
  if (challengeValid_ && uint32_t(nowMs - challengeCreatedAtMs_) >= CHALLENGE_TIMEOUT_MS) {
    challengeValid_ = false;
    activeChallenge_ = 0;
  }

  // Check authenticated session inactivity timeout (5 minutes)
  if (sessionAuthorized_ && uint32_t(nowMs - sessionLastActivityMs_) >= SESSION_TIMEOUT_MS) {
    sessionAuthorized_ = false;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Authenticated session expired due to inactivity.");
#endif
  }
}

bool ConfigManager::beginTransaction() {
  staged_ = active_;
  stagedTxActive_ = true;
  stagedTxStartMs_ = millis();
  return true;
}

bool ConfigManager::commitTransaction() {
  if (!stagedTxActive_) return false;
  if (!validateConfig(staged_)) return false;

  active_ = staged_;
  active_.activeRevision++;
  stagedTxActive_ = false;
  return true;
}

void ConfigManager::abortTransaction() {
  stagedTxActive_ = false;
}

BleConfigStatusCode ConfigManager::setPower(bool power, bool isStaged) {
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.isPowerOn = power;
    return BLE_CFG_STATUS_OK;
  }
  active_.isPowerOn = power;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setBrightness(uint8_t brightness, bool isStaged) {
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.brightness = brightness;
    return BLE_CFG_STATUS_OK;
  }
  active_.brightness = brightness;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setGlobalRgb(RgbColor color, bool isStaged) {
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.globalColor = color;
    for (uint8_t i = 0; i < 6; ++i) staged_.ledColors[i] = color;
    return BLE_CFG_STATUS_OK;
  }
  active_.globalColor = color;
  for (uint8_t i = 0; i < 6; ++i) active_.ledColors[i] = color;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setLedRgb(uint8_t index, RgbColor color, bool isStaged) {
  if (index >= 6) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.ledColors[index] = color;
    return BLE_CFG_STATUS_OK;
  }
  active_.ledColors[index] = color;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setEffect(uint8_t effectId, uint8_t speed, bool isStaged) {
  if (effectId > 4 || speed > 100) return BLE_CFG_STATUS_INVALID_PARAM;
  if (effectId == 4 && !active_.calibrationVerified) {
    return BLE_CFG_STATUS_INVALID_STATE; // RPM Bar forbidden while calibration unverified
  }
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.effect = effectId;
    staged_.effectSpeed = speed;
    return BLE_CFG_STATUS_OK;
  }
  active_.effect = effectId;
  active_.effectSpeed = speed;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setStartupAnimation(uint8_t animId, uint16_t durationMs, bool isStaged) {
  if (animId > 3 || durationMs < 100 || durationMs > 10000) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.startupAnimation = animId;
    staged_.startupDurationMs = durationMs;
    return BLE_CFG_STATUS_OK;
  }
  active_.startupAnimation = animId;
  active_.startupDurationMs = durationMs;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setLedThreshold(uint8_t index, uint16_t rpm, bool isStaged) {
  if (index >= 6 || rpm > 20000) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.rpmThresholds[index] = rpm;
    return BLE_CFG_STATUS_OK;
  }
  // Validate ordering before single field commit
  uint16_t temp[6];
  memcpy(temp, active_.rpmThresholds, sizeof(temp));
  temp[index] = rpm;
  for (uint8_t i = 1; i < 6; ++i) {
    if (temp[i] < temp[i - 1]) return BLE_CFG_STATUS_VALIDATION_FAILED;
  }
  if (active_.warningRpm < temp[5]) return BLE_CFG_STATUS_VALIDATION_FAILED;

  active_.rpmThresholds[index] = rpm;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setShiftWarning(uint16_t warningRpm, uint8_t patternId, bool isStaged) {
  if (warningRpm > 20000 || patternId > 3) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.warningRpm = warningRpm;
    staged_.warningPatternId = patternId;
    return BLE_CFG_STATUS_OK;
  }
  if (warningRpm < active_.rpmThresholds[5]) return BLE_CFG_STATUS_VALIDATION_FAILED;

  active_.warningRpm = warningRpm;
  active_.warningPatternId = patternId;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setRpmMultiplier(float multiplier, bool isStaged) {
  if (isnan(multiplier) || isinf(multiplier) || multiplier < 0.1f || multiplier > 1000.0f) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.rpmMultiplier = multiplier;
    staged_.calibrationVerified = false; // Invalidate verification
    return BLE_CFG_STATUS_OK;
  }
  active_.rpmMultiplier = multiplier;
  active_.calibrationVerified = false; // Invalidate verification
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setCandidateSource(uint32_t canId, uint8_t byteIndex, bool isExtended, bool isStaged) {
  if (byteIndex >= 8) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isExtended) {
    if (canId > 0x1FFFFFFF) return BLE_CFG_STATUS_INVALID_PARAM;
  } else {
    if (canId > 0x7FF) return BLE_CFG_STATUS_INVALID_PARAM;
  }
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.canCandidateId = canId;
    staged_.candidateByteIndex = byteIndex;
    staged_.isExtendedFrame = isExtended;
    staged_.calibrationVerified = false; // Invalidate verification
    return BLE_CFG_STATUS_OK;
  }
  active_.canCandidateId = canId;
  active_.candidateByteIndex = byteIndex;
  active_.isExtendedFrame = isExtended;
  active_.calibrationVerified = false; // Invalidate verification
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setLedDirection(uint8_t direction, bool isStaged) {
  if (direction > 1) return BLE_CFG_STATUS_INVALID_PARAM;
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.ledDirection = direction;
    return BLE_CFG_STATUS_OK;
  }
  active_.ledDirection = direction;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

BleConfigStatusCode ConfigManager::setWarningRgb(RgbColor color, bool isStaged) {
  if (isStaged) {
    if (!stagedTxActive_) return BLE_CFG_STATUS_INVALID_STATE;
    staged_.warningColor = color;
    return BLE_CFG_STATUS_OK;
  }
  active_.warningColor = color;
  active_.activeRevision++;
  return BLE_CFG_STATUS_OK;
}

bool ConfigManager::saveToNvs() {
#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;

  NvsConfigBlob blob{};
  blob.magic = NVS_MAGIC;
  blob.version = 2;
  blob.length = sizeof(blob);
  blob.cfg = active_;
  blob.crc32 = computeCrc32(reinterpret_cast<const uint8_t*>(&blob),
                            sizeof(blob) - sizeof(uint32_t));

  const size_t written = prefs.putBytes(NVS_KEY_BLOB, &blob, sizeof(blob));
  prefs.end();
  return (written == sizeof(blob));
#else
  return true;
#endif
}

bool ConfigManager::restoreDefaults(uint8_t conf1, uint8_t conf2) {
  if (conf1 != 0xA5 || conf2 != 0x5A) return false;
  applySafeDefaults(active_);
  active_.activeRevision++;
  saveToNvs();
  clearOwnerEnrollment();
  return true;
}

size_t ConfigManager::serializeSnapshot(uint8_t* outBuf, size_t maxLen) const {
  if (!outBuf || maxLen < 120) return 0;

  size_t offset = 0;
  outBuf[offset++] = active_.schemaVersion; // Byte 0: 2

  // Bytes 1..4: activeRevision (u32 LE)
  outBuf[offset++] = static_cast<uint8_t>(active_.activeRevision & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((active_.activeRevision >> 8) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((active_.activeRevision >> 16) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((active_.activeRevision >> 24) & 0xFF);

  auto appendTlv = [&](uint8_t id, const uint8_t* val, uint8_t len) {
    if (offset + 2 + len > maxLen - 4) return;
    outBuf[offset++] = id;
    outBuf[offset++] = len;
    memcpy(&outBuf[offset], val, len);
    offset += len;
  };

  // TLV 0x10: isPowerOn (1 byte)
  uint8_t pwr = active_.isPowerOn ? 1 : 0;
  appendTlv(0x10, &pwr, 1);

  // TLV 0x11: brightness (1 byte)
  appendTlv(0x11, &active_.brightness, 1);

  // TLV 0x12: globalColor (3 bytes)
  uint8_t gc[3] = {active_.globalColor.r, active_.globalColor.g, active_.globalColor.b};
  appendTlv(0x12, gc, 3);

  // TLV 0x13: ledColors (18 bytes)
  uint8_t lc[18];
  for (uint8_t i = 0; i < 6; ++i) {
    lc[i * 3 + 0] = active_.ledColors[i].r;
    lc[i * 3 + 1] = active_.ledColors[i].g;
    lc[i * 3 + 2] = active_.ledColors[i].b;
  }
  appendTlv(0x13, lc, 18);

  // TLV 0x14: effect & speed (2 bytes)
  uint8_t eff[2] = {active_.effect, active_.effectSpeed};
  appendTlv(0x14, eff, 2);

  // TLV 0x15: startupAnimation & durationMs (3 bytes)
  uint8_t startAnim[3] = {
    active_.startupAnimation,
    static_cast<uint8_t>(active_.startupDurationMs & 0xFF),
    static_cast<uint8_t>((active_.startupDurationMs >> 8) & 0xFF)
  };
  appendTlv(0x15, startAnim, 3);

  // TLV 0x16: rpmThresholds (12 bytes)
  uint8_t th[12];
  for (uint8_t i = 0; i < 6; ++i) {
    th[i * 2 + 0] = static_cast<uint8_t>(active_.rpmThresholds[i] & 0xFF);
    th[i * 2 + 1] = static_cast<uint8_t>((active_.rpmThresholds[i] >> 8) & 0xFF);
  }
  appendTlv(0x16, th, 12);

  // TLV 0x17: warningRpm & warningPatternId (3 bytes)
  uint8_t wrn[3] = {
    static_cast<uint8_t>(active_.warningRpm & 0xFF),
    static_cast<uint8_t>((active_.warningRpm >> 8) & 0xFF),
    active_.warningPatternId
  };
  appendTlv(0x17, wrn, 3);

  // TLV 0x18: rpmMultiplier * 1000 (4 bytes u32 LE)
  uint32_t multX1000 = static_cast<uint32_t>(active_.rpmMultiplier * 1000.0f);
  uint8_t mBytes[4] = {
    static_cast<uint8_t>(multX1000 & 0xFF),
    static_cast<uint8_t>((multX1000 >> 8) & 0xFF),
    static_cast<uint8_t>((multX1000 >> 16) & 0xFF),
    static_cast<uint8_t>((multX1000 >> 24) & 0xFF)
  };
  appendTlv(0x18, mBytes, 4);

  // TLV 0x19: candidate CAN source (6 bytes)
  uint8_t src[6] = {
    static_cast<uint8_t>(active_.canCandidateId & 0xFF),
    static_cast<uint8_t>((active_.canCandidateId >> 8) & 0xFF),
    static_cast<uint8_t>((active_.canCandidateId >> 16) & 0xFF),
    static_cast<uint8_t>((active_.canCandidateId >> 24) & 0xFF),
    active_.candidateByteIndex,
    static_cast<uint8_t>(active_.isExtendedFrame ? 1 : 0)
  };
  appendTlv(0x19, src, 6);

  // TLV 0x1A: ledDirection (1 byte)
  appendTlv(0x1A, &active_.ledDirection, 1);

  // TLV 0x1B: warningColor (3 bytes)
  uint8_t wc[3] = {active_.warningColor.r, active_.warningColor.g, active_.warningColor.b};
  appendTlv(0x1B, wc, 3);

  // Compute CRC32 over all serialized bytes so far
  const uint32_t crc = computeCrc32(outBuf, offset);
  outBuf[offset++] = static_cast<uint8_t>(crc & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 24) & 0xFF);

  return offset;
}

void ConfigManager::triggerAuthWindow(uint32_t nowMs) {
  authWindowStartMs_ = nowMs;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.printf("[SECURITY] Physical BOOT button pressed. Authorization window open for %u seconds.\r\n",
                (unsigned)(AUTH_WINDOW_MS / 1000));
#endif
}

bool ConfigManager::isAuthWindowOpen(uint32_t nowMs) const {
  return (authWindowStartMs_ != 0 && uint32_t(nowMs - authWindowStartMs_) < AUTH_WINDOW_MS);
}

bool ConfigManager::isAuthorized(uint32_t nowMs) const {
  if (isAuthWindowOpen(nowMs)) {
    return true;
  }
  if (sessionAuthorized_ && uint32_t(nowMs - sessionLastActivityMs_) < SESSION_TIMEOUT_MS) {
    return true;
  }
  return false;
}

void ConfigManager::refreshSessionActivity(uint32_t nowMs) {
  if (sessionAuthorized_) {
    sessionLastActivityMs_ = nowMs;
  }
}

BleConfigStatusCode ConfigManager::enrollOwner(const uint8_t* secret, size_t len, uint32_t nowMs) {
  // Owner enrollment strictly requires physical presence proof via BOOT authorization window
  if (!isAuthWindowOpen(nowMs)) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Enrollment rejected: Physical BOOT window not open.");
#endif
    return BLE_CFG_STATUS_UNAUTHORIZED;
  }

  if (secret == nullptr || len != OWNER_SECRET_LEN) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }

  // Validate that the secret is not trivial (e.g., not all zeros)
  bool allZero = true;
  for (size_t i = 0; i < len; ++i) {
    if (secret[i] != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    return BLE_CFG_STATUS_INVALID_PARAM;
  }

  // Store in memory
  memcpy(ownerSecret_, secret, OWNER_SECRET_LEN);
  ownerEnrolled_ = true;
  sessionAuthorized_ = true;
  sessionLastActivityMs_ = nowMs;
  authFailureCount_ = 0;

  // Persist to NVS
#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  Preferences secPrefs;
  if (secPrefs.begin(NVS_SEC_NAMESPACE, false)) {
    OwnerSecurityBlob secBlob{};
    secBlob.magic = NVS_SEC_MAGIC;
    secBlob.isEnrolled = 1;
    memcpy(secBlob.ownerSecret, ownerSecret_, OWNER_SECRET_LEN);
    secBlob.enrollTimestamp = nowMs;
    secBlob.crc32 = computeCrc32(reinterpret_cast<const uint8_t*>(&secBlob),
                                 sizeof(secBlob) - sizeof(uint32_t));
    secPrefs.putBytes(NVS_KEY_CREDS, &secBlob, sizeof(secBlob));
    secPrefs.end();
  }
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[SECURITY] New owner enrolled and credentials persisted to NVS.");
#endif
#endif

  return BLE_CFG_STATUS_OK;
}

bool ConfigManager::requestChallenge(uint32_t nowMs, uint32_t& outChallenge) {
  // If owner is not enrolled, challenge-response cannot be performed
  if (!ownerEnrolled_) {
    return false;
  }

  // Rate-limiting check: if under lockout due to failed attempts
  if (authFailureCount_ >= 5 && uint32_t(nowMs - lastAuthFailureMs_) < 3000) {
    return false;
  }

#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  activeChallenge_ = esp_random();
#else
  static uint32_t s_mockRng = 0x12345678;
  s_mockRng = s_mockRng * 1664525u + 1013904223u;
  activeChallenge_ = s_mockRng;
#endif

  // Avoid generating 0 as a valid challenge
  if (activeChallenge_ == 0) {
    activeChallenge_ = 0x5A5AA5A5;
  }

  challengeCreatedAtMs_ = nowMs;
  challengeValid_ = true;
  outChallenge = activeChallenge_;
  return true;
}

void ConfigManager::computeAuthProof(const uint8_t secret[OWNER_SECRET_LEN], uint32_t challenge, uint16_t sequence, uint8_t outProof[8]) {
  uint8_t msg[18]; // 12 bytes secret + 4 bytes challenge + 2 bytes sequence
  memcpy(&msg[0], secret, OWNER_SECRET_LEN);
  msg[12] = static_cast<uint8_t>(challenge & 0xFF);
  msg[13] = static_cast<uint8_t>((challenge >> 8) & 0xFF);
  msg[14] = static_cast<uint8_t>((challenge >> 16) & 0xFF);
  msg[15] = static_cast<uint8_t>((challenge >> 24) & 0xFF);
  msg[16] = static_cast<uint8_t>(sequence & 0xFF);
  msg[17] = static_cast<uint8_t>((sequence >> 8) & 0xFF);

#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  uint8_t fullHash[32];
  mbedtls_sha256(msg, sizeof(msg), fullHash, 0); // 0 = SHA-256
  memcpy(outProof, fullHash, 8);
#else
  // Host test stub SHA-256 / digest simulation
  const uint32_t c1 = computeCrc32(msg, 9);
  const uint32_t c2 = computeCrc32(msg + 9, 9);
  outProof[0] = static_cast<uint8_t>(c1 & 0xFF);
  outProof[1] = static_cast<uint8_t>((c1 >> 8) & 0xFF);
  outProof[2] = static_cast<uint8_t>((c1 >> 16) & 0xFF);
  outProof[3] = static_cast<uint8_t>((c1 >> 24) & 0xFF);
  outProof[4] = static_cast<uint8_t>(c2 & 0xFF);
  outProof[5] = static_cast<uint8_t>((c2 >> 8) & 0xFF);
  outProof[6] = static_cast<uint8_t>((c2 >> 16) & 0xFF);
  outProof[7] = static_cast<uint8_t>((c2 >> 24) & 0xFF);
#endif
}

BleConfigStatusCode ConfigManager::authenticateChallenge(const uint8_t* proof, size_t len, uint16_t sequence, uint32_t nowMs) {
  // 1. Check physical button window presence first
  if (isAuthWindowOpen(nowMs)) {
    sessionAuthorized_ = true;
    sessionLastActivityMs_ = nowMs;
    authFailureCount_ = 0;
    challengeValid_ = false;
    activeChallenge_ = 0;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Session authorized via physical BOOT window presence.");
#endif
    return BLE_CFG_STATUS_OK;
  }

  // 2. Reject if owner has never been enrolled
  if (!ownerEnrolled_) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Authentication rejected: Owner un-enrolled. Press BOOT button.");
#endif
    return BLE_CFG_STATUS_UNAUTHORIZED;
  }

  // 3. Enforce rate-limiting lockout after repeated failures (5 attempts -> 3s cooldown)
  if (authFailureCount_ >= 5 && uint32_t(nowMs - lastAuthFailureMs_) < 3000) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Authentication rate-limited; retry shortly.");
#endif
    return BLE_CFG_STATUS_BUSY;
  }

  // 4. Verify single-use challenge validity and expiration (30 seconds TTL)
  if (!challengeValid_ || uint32_t(nowMs - challengeCreatedAtMs_) >= CHALLENGE_TIMEOUT_MS) {
    challengeValid_ = false;
    activeChallenge_ = 0;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Authentication rejected: Missing or expired challenge.");
#endif
    return BLE_CFG_STATUS_INVALID_STATE;
  }

  // Single-use guarantee: Invalidate challenge immediately upon presentation to defeat replay attacks
  challengeValid_ = false;
  const uint32_t challengeToVerify = activeChallenge_;
  activeChallenge_ = 0;

  if (proof == nullptr || len != 8) {
    authFailureCount_++;
    lastAuthFailureMs_ = nowMs;
    return BLE_CFG_STATUS_INVALID_PARAM;
  }

  // 5. Compute expected proof and compare in constant time
  uint8_t expectedProof[8];
  computeAuthProof(ownerSecret_, challengeToVerify, sequence, expectedProof);

  uint8_t diff = 0;
  for (size_t i = 0; i < 8; ++i) {
    diff |= (proof[i] ^ expectedProof[i]);
  }

  if (diff != 0) {
    authFailureCount_++;
    lastAuthFailureMs_ = nowMs;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[SECURITY] Authentication failed: Cryptographic proof mismatch.");
#endif
    return BLE_CFG_STATUS_UNAUTHORIZED;
  }

  // Authentication succeeded!
  sessionAuthorized_ = true;
  sessionLastActivityMs_ = nowMs;
  authFailureCount_ = 0;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[SECURITY] Session authorized via challenge-response cryptographic proof.");
#endif
  return BLE_CFG_STATUS_OK;
}

void ConfigManager::clearOwnerEnrollment() {
  ownerEnrolled_ = false;
  memset(ownerSecret_, 0, sizeof(ownerSecret_));
  sessionAuthorized_ = false;
  challengeValid_ = false;
  activeChallenge_ = 0;

#if defined(ARDUINO) && !defined(PULSESHIFT_TEST_BUILD)
  Preferences secPrefs;
  if (secPrefs.begin(NVS_SEC_NAMESPACE, false)) {
    secPrefs.clear();
    secPrefs.end();
  }
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[SECURITY] Owner enrollment erased from NVS.");
#endif
#endif
}

void ConfigManager::onClientDisconnected() {
  sessionAuthorized_ = false;
  challengeValid_ = false;
  activeChallenge_ = 0;
  abortTransaction();
}
