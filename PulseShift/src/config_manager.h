// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ble_protocol.h"
#include "shift_light.h"

struct PulseShiftConfig {
  uint8_t  schemaVersion = 2;
  uint32_t activeRevision = 1;
  bool     isPowerOn = true;
  uint8_t  brightness = 200;
  RgbColor globalColor = {140, 43, 238}; // BrandViolet default
  RgbColor ledColors[6] = {
    {140, 43, 238}, {140, 43, 238}, {140, 43, 238},
    {140, 43, 238}, {140, 43, 238}, {140, 43, 238}
  };
  uint8_t  effect = 0;              // 0=Static, 1=Breathing, 2=Rainbow, 3=Chase, 4=RpmBar
  uint8_t  effectSpeed = 50;        // 0..100
  uint8_t  ledDirection = 0;       // 0=Normal, 1=Reversed
  uint8_t  startupAnimation = 1;   // 0=None, 1=Sweep, 2=Wave, 3=Pulse
  uint16_t startupDurationMs = 2000;
  uint16_t rpmThresholds[6] = {3000, 4000, 5000, 6000, 7000, 8000};
  uint16_t warningRpm = 8500;
  uint8_t  warningPatternId = 0;   // 0=Flash, 1=Strobe, 2=Solid, 3=Off
  RgbColor warningColor = {255, 0, 0};
  float    rpmMultiplier = 40.0f;  // Multiplier * 1000 = 40000
  uint32_t canCandidateId = 0x301;
  uint8_t  candidateByteIndex = 0;
  bool     isExtendedFrame = false;
  bool     calibrationVerified = false; // Strictly UNVERIFIED initially
};

class ConfigManager {
 public:
  static ConfigManager& instance();

  bool begin();
  void poll(uint32_t nowMs);

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

  // Persistence (NVS)
  bool saveToNvs();
  bool restoreDefaults(uint8_t conf1, uint8_t conf2);

  // Snapshot Serialization
  size_t serializeSnapshot(uint8_t* outBuf, size_t maxLen) const;

  // Validation
  static bool validateConfig(const PulseShiftConfig& cfg);

  // Security / Provisioning
  void triggerAuthWindow(uint32_t nowMs);
  bool isAuthWindowOpen(uint32_t nowMs) const;
  bool isAuthorized(uint32_t nowMs) const;
  bool authenticate(const uint8_t* token, size_t len);
  void onClientDisconnected();
  uint8_t securityMode() const { return BLE_SEC_MODE_HYBRID; }

  // Static CRC32 Helper
  static uint32_t computeCrc32(const uint8_t* data, size_t length);

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
  bool sessionAuthorized_ = false;

  void applySafeDefaults(PulseShiftConfig& cfg);
};
