// Copyright (c) 2026 Aritra Saha. All rights reserved.
#if defined(ARDUINO)
  #include <Arduino.h>
#endif

#include "config.h"
#include "ble_manager.h"
#include "storage_manager.h"
#include "diag_logger.h"
#include "config_manager.h"
#include <string.h>
#include <stdio.h>
#include <atomic>

#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
#include <NimBLEDevice.h>

static NimBLEServer* s_pServer = nullptr;
static NimBLECharacteristic* s_pRpmChar = nullptr;
static NimBLECharacteristic* s_pHealthChar = nullptr;
static NimBLECharacteristic* s_pLogsChar = nullptr;
static NimBLECharacteristic* s_pControlChar = nullptr;

// V2 Characteristics
static NimBLECharacteristic* s_pConfigCmdChar = nullptr;
static NimBLECharacteristic* s_pConfigResultChar = nullptr;
static NimBLECharacteristic* s_pConfigSnapshotChar = nullptr;
static NimBLECharacteristic* s_pCapabilitiesChar = nullptr;

static std::atomic<bool> s_clientConnected{false};
static std::atomic<uint16_t> s_connHandle{0};
static std::atomic<bool> s_rpmSubscribed{false};
static std::atomic<bool> s_healthSubscribed{false};
static std::atomic<bool> s_logsSubscribed{false};
static std::atomic<bool> s_configResultSubscribed{false};
static std::atomic<bool> s_configSnapshotSubscribed{false};

class BleServerCallbacksImpl : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    s_clientConnected.store(true, std::memory_order_release);
    s_connHandle.store(connInfo.getConnHandle(), std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Client connected: %s\r\n", connInfo.getAddress().toString().c_str());
#endif
    // Request balanced connection parameters: min 20ms, max 40ms, latency 0, timeout 2000ms
    pServer->updateConnParams(connInfo.getConnHandle(), 16, 32, 0, 200);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer; (void)connInfo; (void)reason;
    s_clientConnected.store(false, std::memory_order_release);
    s_connHandle.store(0, std::memory_order_release);
    s_rpmSubscribed.store(false, std::memory_order_release);
    s_healthSubscribed.store(false, std::memory_order_release);
    s_logsSubscribed.store(false, std::memory_order_release);
    s_configResultSubscribed.store(false, std::memory_order_release);
    s_configSnapshotSubscribed.store(false, std::memory_order_release);
    BleManager::instance().onClientDisconnected();
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Client disconnected (reason %d). Re-advertising...\r\n", reason);
#endif
    NimBLEDevice::startAdvertising();
  }
};

class BleRpmCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    s_rpmSubscribed.store((subValue & 0x01) != 0, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] RPM notifications %s\r\n", s_rpmSubscribed.load(std::memory_order_acquire) ? "ENABLED" : "DISABLED");
#endif
  }
};

class BleHealthCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    s_healthSubscribed.store((subValue & 0x01) != 0, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Health notifications %s\r\n", s_healthSubscribed.load(std::memory_order_acquire) ? "ENABLED" : "DISABLED");
#endif
  }
};

class BleLogCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    s_logsSubscribed.store((subValue & 0x01) != 0, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Log notifications %s\r\n", s_logsSubscribed.load(std::memory_order_acquire) ? "ENABLED" : "DISABLED");
#endif
  }
};

class BleControlCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    const std::string val = pCharacteristic->getValue();
    const uint8_t* data = reinterpret_cast<const uint8_t*>(val.data());
    const size_t len = val.size();

    auto& mgr = BleManager::instance();

    // Protocol v1 Control commands:
    // [0x01, 0x10, offset_u32_LE] -> Request page of historical diagnostic logs (6 bytes)
    // [0x01, 0x11]                -> Cancel historical log retrieval (2 bytes)
    // [0x01, 0x12, startRec_u32]  -> Request page of CAN capture records (6 bytes)
    // [0x01, 0x13]                -> Cancel CAN capture export (2 bytes)
    if (len >= 2 && data[0] == PULSESHIFT_BLE_PROTOCOL_VERSION) {
      if (data[1] == BLE_CTRL_CMD_GET_LOG_PAGE && len == 6) {
        uint32_t offset = 0;
        memcpy(&offset, &data[2], 4);
        mgr.pendingCtrlCmd_.opcode = BLE_CTRL_CMD_GET_LOG_PAGE;
        mgr.pendingCtrlCmd_.offset = offset;
        mgr.pendingCtrlCmd_.pending.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.printf("[BLE] Queued log page request from offset %lu\r\n", (unsigned long)offset);
#endif
        return;
      } else if (data[1] == BLE_CTRL_CMD_CANCEL_LOGS && len == 2) {
        mgr.pendingCtrlCmd_.opcode = BLE_CTRL_CMD_CANCEL_LOGS;
        mgr.pendingCtrlCmd_.offset = 0;
        mgr.pendingCtrlCmd_.pending.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.println("[BLE] Queued log page retrieval cancellation");
#endif
        return;
      } else if (data[1] == BLE_CTRL_CMD_GET_CAPTURE_PAGE && len == 6) {
        uint32_t startRec = 0;
        memcpy(&startRec, &data[2], 4);
        mgr.pendingCtrlCmd_.opcode = BLE_CTRL_CMD_GET_CAPTURE_PAGE;
        mgr.pendingCtrlCmd_.offset = startRec;
        mgr.pendingCtrlCmd_.pending.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.printf("[BLE] Queued capture page request from record %lu\r\n", (unsigned long)startRec);
#endif
        return;
      } else if (data[1] == BLE_CTRL_CMD_CANCEL_CAPTURE && len == 2) {
        mgr.pendingCtrlCmd_.opcode = BLE_CTRL_CMD_CANCEL_CAPTURE;
        mgr.pendingCtrlCmd_.offset = 0;
        mgr.pendingCtrlCmd_.pending.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.println("[BLE] Queued capture export cancellation");
#endif
        return;
      }
    }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Rejected unauthorized / invalid control command (len=%u)\r\n", (unsigned)len);
#endif
  }
};

class BleConfigResultCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    s_configResultSubscribed.store((subValue & 0x01) != 0, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Config Result notifications %s\r\n",
                  s_configResultSubscribed.load(std::memory_order_acquire) ? "ENABLED" : "DISABLED");
#endif
  }
};

class BleConfigSnapshotCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    s_configSnapshotSubscribed.store((subValue & 0x01) != 0, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Config Snapshot notifications %s\r\n",
                  s_configSnapshotSubscribed.load(std::memory_order_acquire) ? "ENABLED" : "DISABLED");
#endif
  }
};

class BleConfigCmdCallbacksImpl : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    const std::string val = pCharacteristic->getValue();
    const uint8_t* data = reinterpret_cast<const uint8_t*>(val.data());
    const size_t len = val.size();

    auto& mgr = BleManager::instance();
    if (len >= 6 && len <= sizeof(BleConfigCommandPacket)) {
      BleConfigCommandPacket pkt{};
      memcpy(&pkt, data, len);
      mgr.pendingConfigCmd_.packet = pkt;
      mgr.pendingConfigCmd_.connId = connInfo.getConnHandle();
      mgr.pendingConfigCmd_.pending.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("[BLE] Queued config command (opcode=0x%02X, seq=%u, len=%u, flags=0x%02X)\r\n",
                    pkt.opcode, pkt.sequence, pkt.payloadLength, pkt.flags);
#endif
    } else {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("[BLE] Rejected malformed config command packet (len=%u)\r\n", (unsigned)len);
#endif
    }
  }
};

static BleServerCallbacksImpl s_serverCallbacks;
static BleRpmCallbacksImpl s_rpmCallbacks;
static BleHealthCallbacksImpl s_healthCallbacks;
static BleLogCallbacksImpl s_logCallbacks;
static BleControlCallbacksImpl s_controlCallbacks;
static BleConfigResultCallbacksImpl s_configResultCallbacks;
static BleConfigSnapshotCallbacksImpl s_configSnapshotCallbacks;
static BleConfigCmdCallbacksImpl s_configCmdCallbacks;

#endif // defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED)

BleManager::BleManager() = default;

BleManager& BleManager::instance() {
  static BleManager inst;
  return inst;
}

void BleManager::onClientDisconnected() {
  histPageActive_ = false;
  histPageEopPending_ = false;
  histCancelRequested_ = false;
  activeMsgInProgress_ = false;
  histLinesSent_ = 0;
  snapshotActive_ = false;
  ConfigManager::instance().onClientDisconnected();
}

bool BleManager::isClientConnected() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isRpmSubscribed() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire) && s_rpmSubscribed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isHealthSubscribed() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire) && s_healthSubscribed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isLogsSubscribed() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire) && s_logsSubscribed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isConfigResultSubscribed() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire) && s_configResultSubscribed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isConfigSnapshotSubscribed() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  return s_clientConnected.load(std::memory_order_acquire) && s_configSnapshotSubscribed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool BleManager::isAdvertising() const {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  return pAdvertising && pAdvertising->isAdvertising();
#else
  return false;
#endif
}

void BleManager::printStatus() const {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("=== PULSESHIFT BLE PERIPHERAL STATUS ===");
  Serial.printf("Device Name: PulseShift | Service UUID: %s\r\n", PULSESHIFT_BLE_SERVICE_UUID);
  Serial.printf("Enabled: %s | Advertising: %s\r\n", isEnabled() ? "YES" : "NO", isAdvertising() ? "YES" : "NO");
  Serial.printf("Client Connected: %s\r\n", isClientConnected() ? "YES" : "NO");
  Serial.printf("Subscriptions: RPM=%s, Health=%s, Logs=%s\r\n",
                isRpmSubscribed() ? "YES" : "NO",
                isHealthSubscribed() ? "YES" : "NO",
                isLogsSubscribed() ? "YES" : "NO");
  Serial.printf("Notifications Sent: RPM=%lu, Health=%lu, Logs=%lu (Total=%lu) | Drops=%lu\r\n",
                (unsigned long)rpmNotifyCount_,
                (unsigned long)healthNotifyCount_,
                (unsigned long)logNotifyCount_,
                (unsigned long)notifyCount_,
                (unsigned long)dropCount_);
  Serial.println("Protocol: PulseShift BLE v1 & v2 (fixed 20-byte packed frames, little-endian)");
  Serial.println("========================================");
#endif
}

void BleManager::updateCapabilitiesChar() {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pCapabilitiesChar) return;
  BleCapabilitiesPacket caps{};
  caps.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
  caps.physicalLedCount = 6;
  caps.featureMask = BLE_FEAT_INDIVIDUAL_RGB | BLE_FEAT_EFFECTS |
                     BLE_FEAT_STARTUP_ANIM | BLE_FEAT_RPM_THRESHOLDS |
                     BLE_FEAT_SHIFT_WARNING | BLE_FEAT_RPM_MULTIPLIER |
                     BLE_FEAT_CANDIDATE_SOURCE | BLE_FEAT_PHONE_PRESETS |
                     BLE_FEAT_NVS_PERSISTENCE | BLE_FEAT_AUTH_WRITES;
  caps.currentRevision = ConfigManager::instance().activeRevision();
  caps.hardwarePresetCapacity = 0; // Phone-managed presets
  caps.securityMode = ConfigManager::instance().securityMode();
  s_pCapabilitiesChar->setValue(reinterpret_cast<const uint8_t*>(&caps), sizeof(caps));
#endif
}

bool BleManager::begin() {
  if (initialized_) return true;

#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  NimBLEDevice::init("PulseShift");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum transmit power (+9dBm)

  s_pServer = NimBLEDevice::createServer();
  s_pServer->setCallbacks(&s_serverCallbacks);

  NimBLEService* pService = s_pServer->createService(PULSESHIFT_BLE_SERVICE_UUID);
  if (!pService) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("[BLE ERROR] Failed to create primary GATT service!");
#endif
    return false;
  }

  // Characteristic 1: RPM telemetry (READ, NOTIFY)
  s_pRpmChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_RPM_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  if (s_pRpmChar) {
    s_pRpmChar->setCallbacks(&s_rpmCallbacks);
    BleRpmPacket initialRpm{};
    initialRpm.version = PULSESHIFT_BLE_PROTOCOL_VERSION;
    initialRpm.provisionalRpm = 65535;
    initialRpm.candidateAgeMs = 65535;
    s_pRpmChar->setValue(reinterpret_cast<const uint8_t*>(&initialRpm), sizeof(initialRpm));
  }

  // Characteristic 2: Health telemetry (READ, NOTIFY)
  s_pHealthChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_HEALTH_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  if (s_pHealthChar) {
    s_pHealthChar->setCallbacks(&s_healthCallbacks);
    BleHealthPacket initialHealth{};
    initialHealth.version = PULSESHIFT_BLE_PROTOCOL_VERSION;
    s_pHealthChar->setValue(reinterpret_cast<const uint8_t*>(&initialHealth), sizeof(initialHealth));
  }

  // Characteristic 3: Diagnostic log chunks (NOTIFY)
  s_pLogsChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_LOGS_UUID,
      NIMBLE_PROPERTY::NOTIFY);
  if (s_pLogsChar) {
    s_pLogsChar->setCallbacks(&s_logCallbacks);
  }

  // Characteristic 4: Read-only control requests (WRITE)
  s_pControlChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_CONTROL_UUID,
      NIMBLE_PROPERTY::WRITE);
  if (s_pControlChar) {
    s_pControlChar->setCallbacks(&s_controlCallbacks);
  }

  // Characteristic 5: Configuration Commands (WRITE)
  s_pConfigCmdChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_CONFIG_CMD_UUID,
      NIMBLE_PROPERTY::WRITE);
  if (s_pConfigCmdChar) {
    s_pConfigCmdChar->setCallbacks(&s_configCmdCallbacks);
  }

  // Characteristic 6: Configuration Results (READ, NOTIFY)
  s_pConfigResultChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_CONFIG_RESULT_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  if (s_pConfigResultChar) {
    s_pConfigResultChar->setCallbacks(&s_configResultCallbacks);
    BleConfigResultPacket initialResult{};
    initialResult.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
    s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&initialResult), sizeof(initialResult));
  }

  // Characteristic 7: Configuration Snapshots (NOTIFY)
  s_pConfigSnapshotChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_CONFIG_SNAPSHOT_UUID,
      NIMBLE_PROPERTY::NOTIFY);
  if (s_pConfigSnapshotChar) {
    s_pConfigSnapshotChar->setCallbacks(&s_configSnapshotCallbacks);
  }

  // Characteristic 8: Capabilities (READ)
  s_pCapabilitiesChar = pService->createCharacteristic(
      PULSESHIFT_BLE_CHAR_CAPABILITIES_UUID,
      NIMBLE_PROPERTY::READ);
  if (s_pCapabilitiesChar) {
    updateCapabilitiesChar();
  }

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName("PulseShift");
  pAdvertising->addServiceUUID(PULSESHIFT_BLE_SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[BLE] PulseShift BLE peripheral active. Advertising as 'PulseShift'");
#endif

  initialized_ = true;
  return true;
#else
  initialized_ = true;
  return true;
#endif
}

void BleManager::queueLiveLog(const char* text) {
  if (!text || text[0] == '\0') return;

  // Don't enqueue if no client is subscribed to logs
  if (!isLogsSubscribed()) return;

  if (liveLogCount_ >= LIVE_LOG_CAPACITY) {
    // Drop oldest message to prevent unbounded buffering
    liveLogTail_ = (liveLogTail_ + 1) % LIVE_LOG_CAPACITY;
    --liveLogCount_;
    ++dropCount_;
  }

  strncpy(liveLogRing_[liveLogHead_], text, LIVE_LOG_MAX_LEN - 1);
  liveLogRing_[liveLogHead_][LIVE_LOG_MAX_LEN - 1] = '\0';
  liveLogHead_ = (liveLogHead_ + 1) % LIVE_LOG_CAPACITY;
  ++liveLogCount_;
}

void BleManager::poll(uint32_t nowMs,
                      RpmCandidateMonitor& candidate,
                      const CanTransport& transport,
                      const CanRecorder& recorder,
                      const RpmState& rpm) {
  if (!initialized_) return;

  if (!isClientConnected()) {
    if (histPageActive_ || activeMsgInProgress_ || snapshotActive_) {
      onClientDisconnected();
    }
    return;
  }

  // 0a. Handle any decoupled control commands received via BLE write callbacks
  if (pendingCtrlCmd_.pending.load(std::memory_order_acquire)) {
    const uint8_t op = pendingCtrlCmd_.opcode;
    const uint32_t off = pendingCtrlCmd_.offset;
    pendingCtrlCmd_.pending.store(false, std::memory_order_release);

    if (op == BLE_CTRL_CMD_GET_LOG_PAGE) {
      histFileOffset_ = off;
      histLinesSent_ = 0;
      histFinished_ = false;
      histCancelRequested_ = false;
      histPageEopPending_ = false;
      histPageFinished_ = 0;
      activeMsgInProgress_ = false;
      histPageActive_ = true;
    } else if (op == BLE_CTRL_CMD_CANCEL_LOGS) {
      histCancelRequested_ = true;
      histPageActive_ = false;
      histPageEopPending_ = false;
      activeMsgInProgress_ = false;
      histLinesSent_ = 0;
    }
  }

  // 0b. Process any incoming Configuration V2 commands
  processConfigCommand(nowMs, candidate);

  // 1. Live RPM telemetry: send at ~10 Hz (every 100 ms) when subscribed
  if (isRpmSubscribed() && uint32_t(nowMs - lastRpmNotifyMs_) >= 100) {
    lastRpmNotifyMs_ = nowMs;
    sendRpmNotification(nowMs, candidate, transport, recorder, rpm);
  }

  // 2. Health telemetry: send at ~1 Hz (every 1000 ms) when subscribed
  if (isHealthSubscribed() && uint32_t(nowMs - lastHealthNotifyMs_) >= 1000) {
    lastHealthNotifyMs_ = nowMs;
    sendHealthNotification(transport, recorder);
  }

  // 3. Service diagnostic log chunks & config snapshots with strict time-based pacing (20 ms between consecutive notifications)
  if ((uint32_t(nowMs - lastLogNotifyMs_) >= LOG_NOTIFICATION_INTERVAL_MS)) {
    if (snapshotActive_) {
      lastLogNotifyMs_ = nowMs;
      processSnapshotStream();
    } else if (isLogsSubscribed()) {
      if (histPageEopPending_) {
        lastLogNotifyMs_ = nowMs;
        processHistoricalPageRead();
      } else if (activeMsgInProgress_) {
        lastLogNotifyMs_ = nowMs;
        processLogQueue();
      } else if (histPageActive_) {
        lastLogNotifyMs_ = nowMs;
        processHistoricalPageRead();
      } else if (liveLogCount_ > 0) {
        lastLogNotifyMs_ = nowMs;
        processLogQueue();
      }
    }
  }
}

void BleManager::sendRpmNotification(uint32_t nowMs,
                                     const RpmCandidateMonitor& candidate,
                                     const CanTransport& transport,
                                     const CanRecorder& recorder,
                                     const RpmState& rpm) {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pRpmChar) return;

  BleRpmPacket pkt{};
  pkt.version = PULSESHIFT_BLE_PROTOCOL_VERSION;

  const bool fresh = candidate.isFresh(nowMs);
  uint8_t flags = 0;
  if (fresh) flags |= BLE_RPM_FLAG_CANDIDATE_FRESH;
  if (candidate.isCalibrated()) flags |= BLE_RPM_FLAG_CALIBRATION_VERIFIED;
  if (rpm.mapped()) flags |= BLE_RPM_FLAG_RPM_MAPPED;
  if (candidate.isShiftLightEnabled()) flags |= BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED;
  if (transport.healthy()) flags |= BLE_RPM_FLAG_CAN_HEALTHY;
  if (transport.hasTraffic(nowMs)) flags |= BLE_RPM_FLAG_TRAFFIC_ACTIVE;
  if (recorder.isRecording() && !recorder.isFlashPreserved() && !recorder.isStorageFull()) {
    flags |= BLE_RPM_FLAG_RECORDING_ACTIVE;
  }
  if (recorder.isFlashPreserved()) {
    flags |= BLE_RPM_FLAG_CAPTURE_PRESERVED;
  }
  pkt.statusFlags = flags;

  pkt.sequenceNumber = rpmSeq_++;
  pkt.uptimeMs = nowMs;

  if (fresh) {
    pkt.provisionalRpm = static_cast<uint16_t>(candidate.estimatedRpm(nowMs));
    pkt.candidateRawByte = candidate.rawValue();
    pkt.candidateAgeMs = static_cast<uint16_t>(candidate.lastUpdateAgeMs(nowMs));
  } else {
    pkt.provisionalRpm = 65535; // Standard unavailable indicator
    pkt.candidateRawByte = 0;
    if (candidate.receivedCount() > 0) {
      const uint32_t age = candidate.lastUpdateAgeMs(nowMs);
      pkt.candidateAgeMs = (age <= 65534) ? static_cast<uint16_t>(age) : 65534;
    } else {
      pkt.candidateAgeMs = 65535;
    }
  }

  pkt.captureStage = static_cast<uint8_t>(recorder.currentStage());
  pkt.canFramesRx = transport.totalRx();
  pkt.reserved = 0;

  s_pRpmChar->setValue(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  s_pRpmChar->notify();
  ++notifyCount_;
  ++rpmNotifyCount_;
#else
  (void)nowMs; (void)candidate; (void)transport; (void)recorder; (void)rpm;
#endif
}

void BleManager::sendHealthNotification(const CanTransport& transport,
                                        const CanRecorder& recorder) {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pHealthChar) return;

  BleHealthPacket pkt{};
  pkt.version = PULSESHIFT_BLE_PROTOCOL_VERSION;

  uint8_t flags = 0;
  auto& sm = StorageManager::instance();
  if (sm.isMounted() && !sm.mountFailed()) flags |= BLE_HEALTH_FLAG_STORAGE_MOUNTED;
  if (sm.isStorageFull() || recorder.isStorageFull()) flags |= BLE_HEALTH_FLAG_STORAGE_FULL;
  if (transport.missedFrames() > 0 || transport.overrunFrames() > 0 || recorder.droppedFrames() > 0) {
    flags |= BLE_HEALTH_FLAG_RX_DEGRADED;
  }
  if (s_clientConnected) flags |= BLE_HEALTH_FLAG_CLIENT_CONNECTED;
  pkt.healthFlags = flags;

  pkt.sequenceNumber = healthSeq_++;
  pkt.missedFrames = transport.missedFrames();
  pkt.overrunFrames = transport.overrunFrames();
  pkt.busErrors = transport.busErrors();
  pkt.recordedFrames = recorder.recordedFrames();

  s_pHealthChar->setValue(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  s_pHealthChar->notify();
  ++notifyCount_;
  ++healthNotifyCount_;
#else
  (void)transport; (void)recorder;
#endif
}

void BleManager::processLogQueue() {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pLogsChar) return;

  if (!activeMsgInProgress_) {
    if (liveLogCount_ == 0) return;

    // Load next live message
    strncpy(activeMsgBuf_, liveLogRing_[liveLogTail_], sizeof(activeMsgBuf_) - 1);
    activeMsgBuf_[sizeof(activeMsgBuf_) - 1] = '\0';
    activeMsgLen_ = strlen(activeMsgBuf_);
    activeMsgOffset_ = 0;
    activeChunkIndex_ = 0;
    activeMsgId_ = logMsgId_++;
    activeMsgInProgress_ = true;

    liveLogTail_ = (liveLogTail_ + 1) % LIVE_LOG_CAPACITY;
    --liveLogCount_;
  }

  // Send next chunk of current active message (up to 14 payload bytes)
  const size_t remaining = activeMsgLen_ - activeMsgOffset_;
  const uint8_t chunkPayloadLen = (remaining > 14) ? 14 : static_cast<uint8_t>(remaining);

  BleLogChunkPacket chunk{};
  chunk.version = PULSESHIFT_BLE_PROTOCOL_VERSION;
  chunk.messageId = activeMsgId_;
  chunk.chunkIndex = activeChunkIndex_++;
  chunk.payloadLength = chunkPayloadLen;

  uint8_t flags = 0;
  if (chunk.chunkIndex == 0) flags |= BLE_LOG_FLAG_FIRST_CHUNK;
  if (chunkPayloadLen == remaining) flags |= BLE_LOG_FLAG_FINAL_CHUNK;
  chunk.flags = flags;

  memcpy(chunk.payload, &activeMsgBuf_[activeMsgOffset_], chunkPayloadLen);
  activeMsgOffset_ += chunkPayloadLen;

  s_pLogsChar->setValue(reinterpret_cast<const uint8_t*>(&chunk), sizeof(chunk));
  s_pLogsChar->notify();
  ++notifyCount_;
  ++logNotifyCount_;

  if (activeMsgOffset_ >= activeMsgLen_) {
    activeMsgInProgress_ = false;
  }
#endif
}

void BleManager::processHistoricalPageRead() {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pLogsChar) return;

  if (histCancelRequested_) {
    histPageActive_ = false;
    histPageEopPending_ = false;
    activeMsgInProgress_ = false;
    return;
  }

  // If a pending End-of-Page notification was deferred until message chunks finished
  if (histPageEopPending_) {
    if (activeMsgInProgress_) {
      // Must finish transmitting all chunks of the message first!
      processLogQueue();
      return;
    }
    // Now transmit the End-of-Page marker with its own dedicated paced transmission
    BleLogChunkPacket eop{};
    eop.version = PULSESHIFT_BLE_PROTOCOL_VERSION;
    eop.flags = BLE_LOG_FLAG_END_OF_PAGE | BLE_LOG_FLAG_FINAL_CHUNK;
    eop.messageId = logMsgId_++;
    eop.chunkIndex = 0;
    eop.payloadLength = 5;
    memcpy(&eop.payload[0], &histFileOffset_, 4);
    eop.payload[4] = histPageFinished_;
    s_pLogsChar->setValue(reinterpret_cast<const uint8_t*>(&eop), sizeof(eop));
    s_pLogsChar->notify();
    ++notifyCount_;
    ++logNotifyCount_;

    histPageEopPending_ = false;
    histPageActive_ = false;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Historical log page completed (nextOffset=%lu, finished=%u)\r\n",
                  (unsigned long)histFileOffset_, histPageFinished_);
#endif
    return;
  }

  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || !sm.exists(DiagLogger::LOG_PATH)) {
    histFileOffset_ = 0;
    histPageFinished_ = 1;
    histPageEopPending_ = true;
    histPageActive_ = false;
    return;
  }

  File f = sm.open(DiagLogger::LOG_PATH, "r");
  if (!f) {
    histPageActive_ = false;
    return;
  }

  const size_t fileSize = f.size();
  if (histFileOffset_ >= fileSize) {
    f.close();
    histPageFinished_ = 1;
    histPageEopPending_ = true;
    histPageActive_ = false;
    return;
  }

  f.seek(histFileOffset_);

  // Read one line (bounded up to 128 chars)
  char lineBuf[LIVE_LOG_MAX_LEN]{};
  size_t bytesRead = 0;
  int ch;
  while (bytesRead < sizeof(lineBuf) - 1 && (ch = f.read()) != -1) {
    lineBuf[bytesRead++] = static_cast<char>(ch);
    if (ch == '\n') break;
  }
  lineBuf[bytesRead] = '\0';
  histFileOffset_ = f.position();
  f.close();

  if (bytesRead > 0) {
    // Stage this line as the active message
    strncpy(activeMsgBuf_, lineBuf, sizeof(activeMsgBuf_) - 1);
    activeMsgBuf_[sizeof(activeMsgBuf_) - 1] = '\0';
    activeMsgLen_ = strlen(activeMsgBuf_);
    activeMsgOffset_ = 0;
    activeChunkIndex_ = 0;
    activeMsgId_ = logMsgId_++;
    activeMsgInProgress_ = true;

    // Send chunk 0 now
    processLogQueue();

    ++histLinesSent_;
    if (histLinesSent_ >= 20 || histFileOffset_ >= fileSize) {
      // Reached 20 lines or EOF; defer EOP marker to its own dedicated paced tick
      histPageFinished_ = (histFileOffset_ >= fileSize) ? 1 : 0;
      histPageEopPending_ = true;
      histPageActive_ = false;
    }
  } else {
    histPageFinished_ = 1;
    histPageEopPending_ = true;
    histPageActive_ = false;
  }
#endif
}

void BleManager::processConfigCommand(uint32_t nowMs, RpmCandidateMonitor& candidate) {
  if (!pendingConfigCmd_.pending.load(std::memory_order_acquire)) return;

  BleConfigCommandPacket pkt = pendingConfigCmd_.packet;
  pendingConfigCmd_.pending.store(false, std::memory_order_release);

  BleConfigResultPacket res{};
  res.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
  res.respondedOpcode = pkt.opcode;
  res.sequence = pkt.sequence;

  if (pkt.version != PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION) {
    res.statusCode = BLE_CFG_STATUS_VERSION_MISMATCH;
    res.resultFlags = 0;
    res.activeRevision = ConfigManager::instance().activeRevision();
    res.payloadLength = 0;
    if (s_pConfigResultChar) {
      s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&res), sizeof(res));
      if (isConfigResultSubscribed()) s_pConfigResultChar->notify();
    }
    return;
  }

  if ((pkt.flags & ~BLE_CFG_FLAG_STAGED) != 0) {
    res.statusCode = BLE_CFG_STATUS_INVALID_PARAM;
    res.resultFlags = 0;
    res.activeRevision = ConfigManager::instance().activeRevision();
    res.payloadLength = 0;
    if (s_pConfigResultChar) {
      s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&res), sizeof(res));
      if (isConfigResultSubscribed()) s_pConfigResultChar->notify();
    }
    return;
  }

  const bool isStaged = (pkt.flags & BLE_CFG_FLAG_STAGED) != 0;
  uint8_t status = BLE_CFG_STATUS_OK;
  uint8_t resFlags = 0;

  // Authorization check
  const bool isReadQuery = (pkt.opcode == BLE_CFG_OP_GET_CAPABILITIES || pkt.opcode == BLE_CFG_OP_GET_SNAPSHOT);
  const bool isAuthFlow = (pkt.opcode == BLE_CFG_OP_AUTHENTICATE ||
                           pkt.opcode == BLE_CFG_OP_REQUEST_CHALLENGE ||
                           pkt.opcode == BLE_CFG_OP_ENROLL_OWNER);
  if (!isReadQuery && !isAuthFlow) {
    if (!ConfigManager::instance().isAuthorized(nowMs)) {
      res.statusCode = BLE_CFG_STATUS_UNAUTHORIZED;
      res.resultFlags = BLE_CFG_RESULT_FLAG_AUTH_REQUIRED;
      res.activeRevision = ConfigManager::instance().activeRevision();
      res.payloadLength = 0;
      if (s_pConfigResultChar) {
        s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&res), sizeof(res));
        if (isConfigResultSubscribed()) s_pConfigResultChar->notify();
      }
      return;
    }
  }

  // Activity refresh for authorized sessions
  ConfigManager::instance().refreshSessionActivity(nowMs);

  switch (pkt.opcode) {
    case BLE_CFG_OP_GET_CAPABILITIES: {
      status = BLE_CFG_STATUS_OK;
      resFlags = 0;
      break;
    }

    case BLE_CFG_OP_GET_SNAPSHOT: {
      status = BLE_CFG_STATUS_OK;
      resFlags = 0;
      snapshotLen_ = ConfigManager::instance().serializeSnapshot(snapshotBuf_, sizeof(snapshotBuf_));
      snapshotId_ = pkt.sequence;
      snapshotTotalChunks_ = static_cast<uint8_t>((snapshotLen_ + 13) / 14);
      snapshotChunkIdx_ = 0;
      snapshotActive_ = true;
      break;
    }

    case BLE_CFG_OP_ENROLL_OWNER: {
      status = ConfigManager::instance().enrollOwner(pkt.payload, pkt.payloadLength, nowMs,
                                                     pendingConfigCmd_.connId);
      if (status == BLE_CFG_STATUS_OK) {
        resFlags |= BLE_CFG_RESULT_FLAG_PERSISTED | BLE_CFG_RESULT_FLAG_APPLIED;
      } else if (status == BLE_CFG_STATUS_UNAUTHORIZED) {
        resFlags |= BLE_CFG_RESULT_FLAG_AUTH_REQUIRED;
      }
      break;
    }

    case BLE_CFG_OP_REQUEST_CHALLENGE: {
      uint32_t challengeNonce = 0;
      bool ok = ConfigManager::instance().requestChallenge(nowMs, challengeNonce);
      if (ok) {
        status = BLE_CFG_STATUS_OK;
        res.payload[0] = static_cast<uint8_t>(challengeNonce & 0xFF);
        res.payload[1] = static_cast<uint8_t>((challengeNonce >> 8) & 0xFF);
        res.payload[2] = static_cast<uint8_t>((challengeNonce >> 16) & 0xFF);
        res.payload[3] = static_cast<uint8_t>((challengeNonce >> 24) & 0xFF);
        res.payloadLength = 4;
      } else {
        if (!ConfigManager::instance().isOwnerEnrolled()) {
          status = BLE_CFG_STATUS_INVALID_STATE;
          resFlags |= BLE_CFG_RESULT_FLAG_AUTH_REQUIRED;
        } else {
          status = BLE_CFG_STATUS_BUSY;
        }
      }
      break;
    }

    case BLE_CFG_OP_AUTHENTICATE: {
      status = ConfigManager::instance().authenticateChallenge(pkt.payload, pkt.payloadLength, pkt.sequence,
                                                               nowMs, pendingConfigCmd_.connId);
      if (status != BLE_CFG_STATUS_OK) {
        resFlags |= BLE_CFG_RESULT_FLAG_AUTH_REQUIRED;
      }
      break;
    }

    case BLE_CFG_OP_SET_POWER: {
      if (pkt.payloadLength < 1) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        status = ConfigManager::instance().setPower(pkt.payload[0] != 0, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_BRIGHTNESS: {
      if (pkt.payloadLength < 1) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        status = ConfigManager::instance().setBrightness(pkt.payload[0], isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_GLOBAL_RGB: {
      if (pkt.payloadLength < 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        RgbColor color{pkt.payload[0], pkt.payload[1], pkt.payload[2]};
        status = ConfigManager::instance().setGlobalRgb(color, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_LED_RGB: {
      if (pkt.payloadLength < 4) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        RgbColor color{pkt.payload[1], pkt.payload[2], pkt.payload[3]};
        status = ConfigManager::instance().setLedRgb(pkt.payload[0], color, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_EFFECT: {
      if (pkt.payloadLength < 2) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        status = ConfigManager::instance().setEffect(pkt.payload[0], pkt.payload[1], isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_STARTUP: {
      if (pkt.payloadLength < 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        uint16_t dur = static_cast<uint16_t>(pkt.payload[1]) | (static_cast<uint16_t>(pkt.payload[2]) << 8);
        status = ConfigManager::instance().setStartupAnimation(pkt.payload[0], dur, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_LED_THRESHOLD: {
      if (pkt.payloadLength < 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        uint16_t rpm = static_cast<uint16_t>(pkt.payload[1]) | (static_cast<uint16_t>(pkt.payload[2]) << 8);
        status = ConfigManager::instance().setLedThreshold(pkt.payload[0], rpm, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_SHIFT_WARNING: {
      if (pkt.payloadLength < 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        uint16_t warnRpm = static_cast<uint16_t>(pkt.payload[0]) | (static_cast<uint16_t>(pkt.payload[1]) << 8);
        status = ConfigManager::instance().setShiftWarning(warnRpm, pkt.payload[2], isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_RPM_MULTIPLIER: {
      if (pkt.payloadLength < 4) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        uint32_t multX1000 = static_cast<uint32_t>(pkt.payload[0]) |
                             (static_cast<uint32_t>(pkt.payload[1]) << 8) |
                             (static_cast<uint32_t>(pkt.payload[2]) << 16) |
                             (static_cast<uint32_t>(pkt.payload[3]) << 24);
        float mult = multX1000 / 1000.0f;
        status = ConfigManager::instance().setRpmMultiplier(mult, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
          if (!isStaged) {
            candidate.setMultiplier(mult);
          }
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_CANDIDATE_SOURCE: {
      if (pkt.payloadLength < 6) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        uint32_t canId = static_cast<uint32_t>(pkt.payload[0]) |
                         (static_cast<uint32_t>(pkt.payload[1]) << 8) |
                         (static_cast<uint32_t>(pkt.payload[2]) << 16) |
                         (static_cast<uint32_t>(pkt.payload[3]) << 24);
        uint8_t byteIdx = pkt.payload[4];
        bool isExt = (pkt.payload[5] != 0);
        status = ConfigManager::instance().setCandidateSource(canId, byteIdx, isExt, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
          if (!isStaged) {
            candidate.setCandidateSource(canId, byteIdx, isExt);
          }
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_LED_DIRECTION: {
      if (pkt.payloadLength < 1) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        status = ConfigManager::instance().setLedDirection(pkt.payload[0], isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_WARNING_RGB: {
      if (pkt.payloadLength < 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        RgbColor color{pkt.payload[0], pkt.payload[1], pkt.payload[2]};
        status = ConfigManager::instance().setWarningRgb(color, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SAVE_CONFIG: {
      bool ok = ConfigManager::instance().saveToNvs();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_STORAGE_ERROR;
      if (ok) {
        resFlags |= BLE_CFG_RESULT_FLAG_PERSISTED | BLE_CFG_RESULT_FLAG_APPLIED;
      }
      break;
    }

    case BLE_CFG_OP_RESTORE_DEFAULTS: {
      if (pkt.payloadLength < 2) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        bool ok = ConfigManager::instance().restoreDefaults(pkt.payload[0], pkt.payload[1]);
        status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_INVALID_PARAM;
        if (ok) {
          resFlags |= BLE_CFG_RESULT_FLAG_PERSISTED | BLE_CFG_RESULT_FLAG_APPLIED;
          candidate.reset();
        }
      }
      break;
    }

    case BLE_CFG_OP_BEGIN_TRANSACTION: {
      bool ok = ConfigManager::instance().beginTransaction();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_INVALID_STATE;
      if (ok) resFlags |= BLE_CFG_RESULT_FLAG_STAGED;
      break;
    }

    case BLE_CFG_OP_COMMIT_TRANSACTION: {
      bool ok = ConfigManager::instance().commitTransaction();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_VALIDATION_FAILED;
      if (ok) {
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        const auto& active = ConfigManager::instance().active();
        candidate.setCandidateSource(active.canCandidateId, active.candidateByteIndex, active.isExtendedFrame);
        candidate.setMultiplier(active.rpmMultiplier);
      }
      break;
    }

    case BLE_CFG_OP_ABORT_TRANSACTION: {
      ConfigManager::instance().abortTransaction();
      status = BLE_CFG_STATUS_OK;
      resFlags = 0;
      break;
    }

    default:
      status = BLE_CFG_STATUS_UNSUPPORTED;
      break;
  }

  res.statusCode = status;
  res.resultFlags = resFlags;
  res.activeRevision = ConfigManager::instance().activeRevision();

  if (s_pConfigResultChar) {
    s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&res), sizeof(res));
    if (isConfigResultSubscribed()) {
      s_pConfigResultChar->notify();
      ++notifyCount_;
      ++configResultNotifyCount_;
    }
  }
  updateCapabilitiesChar();
}

void BleManager::processSnapshotStream() {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!snapshotActive_ || !s_pConfigSnapshotChar) return;

  if (snapshotChunkIdx_ < snapshotTotalChunks_) {
    const size_t offset = snapshotChunkIdx_ * 14;
    const size_t rem = (snapshotLen_ > offset) ? (snapshotLen_ - offset) : 0;
    const uint8_t chunkBytes = static_cast<uint8_t>((rem < 14) ? rem : 14);

    BleConfigSnapshotChunkPacket chunk{};
    chunk.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
    chunk.snapshotId = snapshotId_;
    chunk.chunkIndex = snapshotChunkIdx_;
    chunk.chunkCount = snapshotTotalChunks_;
    chunk.payloadLength = chunkBytes;
    if (chunkBytes > 0) {
      memcpy(chunk.payload, &snapshotBuf_[offset], chunkBytes);
    }

    s_pConfigSnapshotChar->setValue(reinterpret_cast<const uint8_t*>(&chunk), sizeof(chunk));
    if (isConfigSnapshotSubscribed()) {
      s_pConfigSnapshotChar->notify();
      ++notifyCount_;
      ++configSnapshotNotifyCount_;
    }

    ++snapshotChunkIdx_;
    if (snapshotChunkIdx_ >= snapshotTotalChunks_) {
      snapshotActive_ = false;
    }
  } else {
    snapshotActive_ = false;
  }
#endif
}
