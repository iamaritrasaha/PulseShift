// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "config.h"
#include <stdint.h>
#include <stddef.h>
#include "ble_protocol.h"
#include "led_color.h"

// Schema version 3: adds decoder extension fields (byte width, byte order,
// offset, validity clamp), threshold hysteresis and the bound calibration
// verification record. NVS blobs of schema 2 still load (new fields default).
struct PulseShiftConfig {
  uint8_t  schemaVersion = 3;
  uint32_t activeRevision = 1;
  bool     isPowerOn = true;
  uint8_t  brightness = 200;
  RgbColor globalColor = {140, 43, 238}; // BrandViolet default
  RgbColor ledColors[6] = {
    {140, 43, 238}, {140, 43, 238}, {140, 43, 238},
    {140, 43, 238}, {140, 43, 238}, {140, 43, 238}
  };
  uint8_t  effect = 0;              // 0=Static, 1=Breathing, 2=Rainbow, 3=Chase, 4=Verified RPM, 5=Provisional preview (factory boot default)
  uint8_t  effectSpeed = 50;        // 0..100
  uint8_t  ledDirection = 0;       // 0=Normal, 1=Reversed
  uint8_t  startupAnimation = 1;   // 0=None, 1=Sweep, 2=Wave, 3=Pulse
  uint16_t startupDurationMs = 2000;
  uint16_t rpmThresholds[6] = {1000, 2000, 3000, 4000, 5000, 6000};
  uint16_t warningRpm = 6500;
  uint8_t  warningPatternId = 0;   // 0=Flash, 1=Strobe, 2=Solid, 3=Off
  RgbColor warningColor = {255, 0, 0};
  uint16_t rpmHysteresis = 150;    // threshold hysteresis (falling RPM), 0..2000
  // --- RPM decoder (phone-configurable, authoritative) ---
  float    rpmMultiplier = 40.0f;
  float    rpmOffset = 0.0f;       // RPM = raw * multiplier + offset
  uint8_t  candidateByteWidth = 1; // 1 or 2 bytes
  bool     candidateLittleEndian = true;
  uint16_t rpmMaxValid = 12000;    // validity clamp (0..20000)
  uint32_t canCandidateId = 0x301;
  uint8_t  candidateByteIndex = 0;
  bool     isExtendedFrame = false;
  // --- Bound calibration verification record ---
  // Only meaningful while calFingerprint equals the live decoder fingerprint.
  bool     calibrationVerified = false; // Strictly UNVERIFIED initially
  uint32_t calFingerprint = 0;
  uint8_t  calPointsUsed = 0;
  uint8_t  calMaxResidualRpm = 0;  // worst residual of the verification fit
  uint32_t calTimestampMs = 0;
};

// One operating-point observation collected during the mobile-assisted
// calibration session (firmware side of the Android workflow).
struct CalibrationPoint {
  bool     captured = false;
  uint16_t refRpm = 0;            // trustworthy independent reference
  float    rawMean = 0.0f;        // mean raw candidate over the capture window
  float    rawStdDev = 0.0f;      // raw stability within the window
  uint16_t samples = 0;
  uint32_t timestampMs = 0;
};

// Result of a calibration fit attempt; surfaced verbatim to the phone.
struct CalibrationResult {
  BleConfigStatusCode status = BLE_CFG_STATUS_INVALID_STATE;
  float    multiplier = 0.0f;
  float    offset = 0.0f;
  float    maxResidualRpm = 0.0f;
  uint8_t  pointsUsed = 0;
  char     reason[48] = "";
};

class ConfigManager {
 public:
  static ConfigManager& instance();

  bool begin();
  void poll(uint32_t nowMs);

  // Re-read the persisted configuration blob from NVS and apply it to the
  // active state without touching owner credentials or captures. Returns
  // false (active state unchanged) when no valid blob is stored.
  bool reloadFromNvs();

  const PulseShiftConfig& active() const { return active_; }
  PulseShiftConfig& activeMutable() { return active_; }

  uint32_t activeRevision() const { return active_.activeRevision; }

  // Transaction Management
  bool isStagedTransactionActive() const { return stagedTxActive_; }
  bool beginTransaction();
  bool commitTransaction();
  void abortTransaction();

  // Configuration Modification (single-field or staged)
  BleConfigStatusCode setPower(bool power, bool isStaged);
  BleConfigStatusCode setBrightness(uint8_t brightness, bool isStaged);
  BleConfigStatusCode setGlobalRgb(RgbColor color, bool isStaged);
  BleConfigStatusCode setLedRgb(uint8_t index, RgbColor color, bool isStaged);
  BleConfigStatusCode setEffect(uint8_t effectId, uint8_t speed, bool isStaged);
  BleConfigStatusCode setStartupAnimation(uint8_t animId, uint16_t durationMs, bool isStaged);
  BleConfigStatusCode setLedThreshold(uint8_t index, uint16_t rpm, bool isStaged);
  BleConfigStatusCode setShiftWarning(uint16_t warningRpm, uint8_t patternId, bool isStaged);
  BleConfigStatusCode setRpmMultiplier(float multiplier, bool isStaged);
  BleConfigStatusCode setCandidateSource(uint32_t canId, uint8_t byteIndex, bool isExtended, bool isStaged);
  BleConfigStatusCode setLedDirection(uint8_t direction, bool isStaged);
  BleConfigStatusCode setWarningRgb(RgbColor color, bool isStaged);
  // Versioned decoder extension (Phase 2): width, byte order, offset, clamp.
  BleConfigStatusCode setRpmDecoderExt(uint8_t byteWidth, bool littleEndian,
                                       float offsetRpm, uint16_t maxValidRpm, bool isStaged);
  BleConfigStatusCode setRpmHysteresis(uint16_t hysteresisRpm, bool isStaged);

  // Persistence (NVS). Returns false when the save failed; callers must not
  // report persisted success unless this returned true.
  bool saveToNvs();
  bool restoreDefaults(uint8_t conf1, uint8_t conf2, bool* nvsSaved = nullptr);

  // Snapshot Serialization
  size_t serializeSnapshot(uint8_t* outBuf, size_t maxLen) const;

  // Validation
  static bool validateConfig(const PulseShiftConfig& cfg);

  // ---- Mobile-assisted calibration session (Phase 4) ----
  // The phone collects time-stamped raw observations at several operating
  // points, submits trustworthy independent RPM references, and the firmware
  // fits scale/offset, validates the evidence and only then binds a
  // verification record. Verification is bound to the decoder fingerprint.
  static constexpr uint8_t CAL_MAX_POINTS = 6;
  static constexpr uint8_t CAL_MIN_POINTS = 3;      // idle + mid + high minimum
  static constexpr float   CAL_MAX_RESIDUAL_RPM = 150.0f;
  static constexpr float   CAL_MAX_STDDEV_RAW = 1.5f;

  bool beginCalibrationSession(uint32_t nowMs);
  bool isCalibrationSessionActive() const { return calSessionActive_; }
  void abortCalibrationSession();
  // Start a rolling capture window at the current operating point.
  bool startCalibrationCapture(uint32_t nowMs);
  // Feed one raw candidate value (called by the application per fresh frame).
  void feedCalibrationSample(uint16_t raw, uint32_t nowMs);
  // Close the capture window and store the operating point with its reference.
  BleConfigStatusCode commitCalibrationPoint(uint16_t refRpm, uint32_t nowMs);
  // Fit scale/offset over collected points, validate evidence and optionally
  // apply + bind the verification record (applyWhenValid).
  CalibrationResult computeCalibration(bool applyWhenValid, uint32_t nowMs);
  const CalibrationPoint& calibrationPoint(uint8_t index) const { return calPoints_[index]; }
  uint8_t calibrationPointCount() const { return calPointCount_; }
  bool calibrationCaptureActive() const { return calCaptureActive_; }
  uint16_t calibrationCaptureSamples() const { return calCaptureSamples_; }
  uint32_t calibrationCaptureStartMs() const { return calCaptureStartMs_; }

  // Security / Provisioning
  void triggerAuthWindow(uint32_t nowMs);
  void closeAuthWindow() { authWindowStartMs_ = 0; }
  bool isAuthWindowOpen(uint32_t nowMs) const;
  bool isAuthorized(uint32_t nowMs) const;
  // Session authorization is bound to the connection that authenticated.
  bool isAuthorizedFor(uint16_t connId, uint32_t nowMs) const;
  bool isOwnerEnrolled() const { return ownerEnrolled_; }
  bool isSessionAuthorized() const { return sessionAuthorized_; }
  BleConfigStatusCode enrollOwner(const uint8_t* secret, size_t len, uint32_t nowMs, uint16_t connId);
  bool requestChallenge(uint32_t nowMs, uint32_t& outChallenge);
  BleConfigStatusCode authenticateChallenge(const uint8_t* proof, size_t len, uint16_t sequence,
                                            uint32_t nowMs, uint16_t connId);
  void refreshSessionActivity(uint32_t nowMs);
  void onClientDisconnected();
  uint8_t securityMode() const { return BLE_SEC_MODE_HYBRID; }

  static constexpr size_t OWNER_SECRET_LEN = 12;

  // Cryptographic Auth Proof Helper
  static void computeAuthProof(const uint8_t secret[OWNER_SECRET_LEN], uint32_t challenge, uint16_t sequence, uint8_t outProof[8]);

  // Static CRC32 Helper
  static uint32_t computeCrc32(const uint8_t* data, size_t length);

  // NVS persistence outcome for diagnostics (did the last save succeed?)
  bool lastNvsSaveOk() const { return lastNvsSaveOk_; }
  uint32_t lastNvsSaveMs() const { return lastNvsSaveMs_; }

 private:
  ConfigManager();
  ~ConfigManager() = default;

  PulseShiftConfig active_{};
  PulseShiftConfig staged_{};
  bool stagedTxActive_ = false;
  uint32_t stagedTxStartMs_ = 0;
  static constexpr uint32_t TX_TIMEOUT_MS = 30000;

  // Security state
  uint32_t authWindowStartMs_ = 0;
  static constexpr uint32_t AUTH_WINDOW_MS = 60000;
  static constexpr uint32_t SESSION_TIMEOUT_MS = 300000;
  static constexpr uint32_t CHALLENGE_TIMEOUT_MS = 30000;
  bool sessionAuthorized_ = false;
  uint16_t sessionConnId_ = 0;   // connection the session is bound to
  uint32_t sessionLastActivityMs_ = 0;
  bool ownerEnrolled_ = false;
  uint8_t ownerSecret_[16]{};
  uint32_t activeChallenge_ = 0;
  uint32_t challengeCreatedAtMs_ = 0;
  bool challengeValid_ = false;
  uint8_t authFailureCount_ = 0;
  uint32_t lastAuthFailureMs_ = 0;

  // Calibration session state
  bool calSessionActive_ = false;
  bool calCaptureActive_ = false;
  uint32_t calCaptureStartMs_ = 0;
  uint16_t calCaptureSamples_ = 0;
  double calCaptureSum_ = 0.0;
  double calCaptureSumSq_ = 0.0;
  CalibrationPoint calPoints_[CAL_MAX_POINTS]{};
  uint8_t calPointCount_ = 0;
  uint32_t calSessionStartMs_ = 0;
  static constexpr uint32_t CAL_SESSION_TIMEOUT_MS = 600000; // 10 min
  static constexpr uint32_t CAL_CAPTURE_WINDOW_MS = 2000;   // 2 s sampling window

  bool lastNvsSaveOk_ = false;
  uint32_t lastNvsSaveMs_ = 0;

  void applySafeDefaults(PulseShiftConfig& cfg);
  void loadOwnerSecurity();
  // Shared NVS blob read path (schema-3 + schema-2 migration) used by
  // begin() and reloadFromNvs().
  bool loadConfigBlobLocked(PulseShiftConfig& out);
  // Invalidate the verification record; if an RPM-driven effect is active,
  // revert it to a decorative effect so the config always stays valid.
  void invalidateCalibration(PulseShiftConfig& cfg);
};
