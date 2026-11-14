// Copyright (c) 2026 Aritra Saha. All rights reserved.
#if defined(ARDUINO)
  #include <Arduino.h>
#elif defined(PULSESHIFT_TEST_BUILD)
  #include <Arduino.h> // host test stubs provide Serial and millis()
#endif

#include "config.h"
#include "ble_manager.h"
#include "storage_manager.h"
#include "diag_logger.h"
#include "config_manager.h"
#include <string.h>
#include <stdio.h>
#include <atomic>
#if defined(ARDUINO)
#include <esp_system.h>
#endif

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
      uint8_t opcode = 0;
      uint32_t offset = 0;
      bool valid = false;
      if (data[1] == BLE_CTRL_CMD_GET_LOG_PAGE && len == 6) {
        memcpy(&offset, &data[2], 4);
        opcode = BLE_CTRL_CMD_GET_LOG_PAGE;
        valid = true;
      } else if (data[1] == BLE_CTRL_CMD_CANCEL_LOGS && len == 2) {
        opcode = BLE_CTRL_CMD_CANCEL_LOGS;
        valid = true;
      } else if (data[1] == BLE_CTRL_CMD_GET_CAPTURE_PAGE && len == 6) {
        memcpy(&offset, &data[2], 4);
        opcode = BLE_CTRL_CMD_GET_CAPTURE_PAGE;
        valid = true;
      } else if (data[1] == BLE_CTRL_CMD_CANCEL_CAPTURE && len == 2) {
        opcode = BLE_CTRL_CMD_CANCEL_CAPTURE;
        valid = true;
      }

      if (valid) {
        // Bounded SPSC ring: NimBLE task produces, main loop consumes.
        const uint8_t head = mgr.ctrlQueueHead_.load(std::memory_order_relaxed);
        const uint8_t next = static_cast<uint8_t>((head + 1) % BleManager::CTRL_QUEUE_CAPACITY);
        if (next != mgr.ctrlQueueTail_.load(std::memory_order_acquire)) {
          mgr.ctrlQueue_[head].opcode = opcode;
          mgr.ctrlQueue_[head].offset = offset;
          mgr.ctrlQueue_[head].connId = connInfo.getConnHandle();
          mgr.ctrlQueueHead_.store(next, std::memory_order_release);
        } else {
          mgr.droppedCommands_++; // queue full: client is issuing faster than poll() drains
        }
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
      // Bounded SPSC ring: NimBLE task produces, main loop consumes. When the
      // ring is full the incoming command is counted and dropped (never
      // silently overwritten mid-processing).
      const uint8_t head = mgr.configQueueHead_.load(std::memory_order_relaxed);
      const uint8_t next = static_cast<uint8_t>((head + 1) % BleManager::CONFIG_QUEUE_CAPACITY);
      if (next != mgr.configQueueTail_.load(std::memory_order_acquire)) {
        mgr.configQueue_[head].packet = pkt;
        mgr.configQueue_[head].connId = connInfo.getConnHandle();
        mgr.configQueueHead_.store(next, std::memory_order_release);
      } else {
        mgr.droppedCommands_++;
      }
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
  capturePageActive_ = false;
  capturePageEopPending_ = false;
  captureCancelRequested_ = false;
  captureLineStaged_ = false;
  captureRecordsSent_ = 0;
  // Discard any commands still queued by the departed connection. NimBLE
  // commonly reuses connection handles, so residue could otherwise execute
  // later under a new client's freshly-bound session.
  configQueueTail_.store(configQueueHead_.load(std::memory_order_acquire), std::memory_order_release);
  ctrlQueueTail_.store(ctrlQueueHead_.load(std::memory_order_acquire), std::memory_order_release);
  CanRecorder::instance().bleExportCancel();
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
  Serial.printf("Notifications Sent: RPM=%lu, Health=%lu, Logs=%lu (Total=%lu) | Drops=%lu | Dropped Commands=%lu\r\n",
                (unsigned long)rpmNotifyCount_,
                (unsigned long)healthNotifyCount_,
                (unsigned long)logNotifyCount_,
                (unsigned long)notifyCount_,
                (unsigned long)dropCount_,
                (unsigned long)droppedCommands_);
  Serial.printf("Firmware: %u.%u.%u | Security Mode: %u | Owner Enrolled: %s | Session Authorized: %s\r\n",
                PULSESHIFT_FW_VERSION_MAJOR, PULSESHIFT_FW_VERSION_MINOR, PULSESHIFT_FW_VERSION_PATCH,
                ConfigManager::instance().securityMode(),
                ConfigManager::instance().isOwnerEnrolled() ? "YES" : "NO",
                ConfigManager::instance().isSessionAuthorized() ? "YES" : "NO");
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
                     BLE_FEAT_NVS_PERSISTENCE | BLE_FEAT_AUTH_WRITES |
                     BLE_FEAT_DECODER_EXT | BLE_FEAT_HYSTERESIS |
                     BLE_FEAT_CAL_WORKFLOW | BLE_FEAT_DIAGNOSTICS_TLV |
                     BLE_FEAT_CAPTURE_EXPORT | BLE_FEAT_CAPTURE_STAGE |
                     BLE_FEAT_LED_BENCH_TEST;
  caps.currentRevision = ConfigManager::instance().activeRevision();
  caps.hardwarePresetCapacity = 0; // Phone-managed presets
  caps.securityMode = ConfigManager::instance().securityMode();
  caps.firmwareMajor = PULSESHIFT_FW_VERSION_MAJOR;
  caps.firmwareMinor = PULSESHIFT_FW_VERSION_MINOR;
  caps.firmwarePatch = PULSESHIFT_FW_VERSION_PATCH;
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
  queueLiveLog(text, 0);
}

void BleManager::queueLiveLog(const char* text, uint8_t flags) {
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
  liveLogFlags_[liveLogHead_] = flags;
  liveLogHead_ = (liveLogHead_ + 1) % LIVE_LOG_CAPACITY;
  ++liveLogCount_;
}

void BleManager::poll(uint32_t nowMs,
                      RpmCandidateMonitor& candidate,
                      const CanTransport& transport,
                      const CanRecorder& recorder,
                      bool rpmLightsRendering) {
  if (!initialized_) return;

  if (!isClientConnected()) {
    if (histPageActive_ || activeMsgInProgress_ || snapshotActive_ || capturePageActive_) {
      onClientDisconnected();
    }
    return;
  }

  // 0a. Drain decoupled control commands received via BLE write callbacks.
  drainControlQueue(nowMs);

  // 0b. Process one incoming Configuration V2 command per loop pass.
  processConfigCommand(nowMs, candidate, transport, recorder);

  // 1. Live RPM telemetry: send at ~10 Hz (every 100 ms) when subscribed
  if (isRpmSubscribed() && uint32_t(nowMs - lastRpmNotifyMs_) >= 100) {
    lastRpmNotifyMs_ = nowMs;
    sendRpmNotification(nowMs, candidate, transport, recorder, rpmLightsRendering);
  }

  // 2. Health telemetry: send at ~1 Hz (every 1000 ms) when subscribed
  if (isHealthSubscribed() && uint32_t(nowMs - lastHealthNotifyMs_) >= 1000) {
    lastHealthNotifyMs_ = nowMs;
    sendHealthNotification(transport, recorder);
  }

  // 3. Service diagnostic log chunks, CAN capture pages & config snapshots
  //    with strict time-based pacing (20 ms between consecutive notifications)
  if ((uint32_t(nowMs - lastLogNotifyMs_) >= LOG_NOTIFICATION_INTERVAL_MS)) {
    if (snapshotActive_) {
      lastLogNotifyMs_ = nowMs;
      processSnapshotStream();
    } else if (isLogsSubscribed()) {
      if (histPageEopPending_) {
        lastLogNotifyMs_ = nowMs;
        processHistoricalPageRead();
      } else if (capturePageEopPending_) {
        lastLogNotifyMs_ = nowMs;
        processCapturePage();
      } else if (activeMsgInProgress_) {
        lastLogNotifyMs_ = nowMs;
        processLogQueue();
      } else if (capturePageActive_) {
        lastLogNotifyMs_ = nowMs;
        processCapturePage();
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

// Drain the v1 control request queue (called from poll() on the loop task).
void BleManager::drainControlQueue(uint32_t nowMs) {
  while (ctrlQueueTail_.load(std::memory_order_acquire) != ctrlQueueHead_.load(std::memory_order_acquire)) {
    const uint8_t tail = ctrlQueueTail_.load(std::memory_order_relaxed);
    const PendingCtrlCmd cmd = ctrlQueue_[tail];
    ctrlQueueTail_.store(static_cast<uint8_t>((tail + 1) % CTRL_QUEUE_CAPACITY), std::memory_order_release);

    // Data-bearing page fetches return the diagnostic log text and the RAW
    // CAN capture records - vehicle telemetry. They require a session bound
    // to the requesting connection, exactly like mutating config commands.
    // Cancel opcodes stay unauthenticated: they only stop a stream the
    // requester may own and disclose nothing.
    const bool isPageFetch = (cmd.opcode == BLE_CTRL_CMD_GET_LOG_PAGE ||
                              cmd.opcode == BLE_CTRL_CMD_GET_CAPTURE_PAGE);
    if (isPageFetch && !ConfigManager::instance().isAuthorizedFor(cmd.connId, nowMs)) {
      queueLiveLog("AUTH REQUIRED: owner session needed for log/capture export", BLE_LOG_FLAG_ERROR);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("[BLE] Rejected unauthenticated page fetch (opcode=0x%02X): owner session required\r\n", cmd.opcode);
#endif
      DiagLogger::instance().logEvent(DiagLevel::Warn, "BLE_CTRL_UNAUTH",
                                      "Unauthenticated log/capture page fetch rejected (opcode=0x%02X)", cmd.opcode);
      continue;
    }

    if (cmd.opcode == BLE_CTRL_CMD_GET_LOG_PAGE) {
      histFileOffset_ = cmd.offset;
      histLinesSent_ = 0;
      histFinished_ = false;
      histCancelRequested_ = false;
      histPageEopPending_ = false;
      histPageFinished_ = 0;
      activeMsgInProgress_ = false;
      histPageActive_ = true;
    } else if (cmd.opcode == BLE_CTRL_CMD_CANCEL_LOGS) {
      histCancelRequested_ = true;
      histPageActive_ = false;
      histPageEopPending_ = false;
      activeMsgInProgress_ = false;
      histLinesSent_ = 0;
    } else if (cmd.opcode == BLE_CTRL_CMD_GET_CAPTURE_PAGE) {
      captureNextRecord_ = cmd.offset;
      captureRecordsSent_ = 0;
      captureCancelRequested_ = false;
      capturePageEopPending_ = false;
      capturePageFinished_ = 0;
      captureLineStaged_ = false;
      activeMsgInProgress_ = false;
      capturePageActive_ = true;
      // Activate the recorder's export cursor and open its page-scoped read
      // handle. An out-of-range start simply stays inactive so the first
      // processCapturePage() reports a clean end-of-capture page.
      CanRecorder::instance().bleExportBegin(cmd.offset);
    } else if (cmd.opcode == BLE_CTRL_CMD_CANCEL_CAPTURE) {
      captureCancelRequested_ = true;
      capturePageActive_ = false;
      capturePageEopPending_ = false;
      captureLineStaged_ = false;
      captureRecordsSent_ = 0;
    }
  }
}

uint8_t BleManager::computeRpmStatusFlags(const RpmCandidateMonitor& candidate,
                                          const CanTransport& transport,
                                          const CanRecorder& recorder,
                                          uint32_t nowMs,
                                          bool rpmLightsRendering) {
  uint8_t flags = 0;
  if (candidate.isFresh(nowMs)) flags |= BLE_RPM_FLAG_CANDIDATE_FRESH;
  if (candidate.isCalibrated()) flags |= BLE_RPM_FLAG_CALIBRATION_VERIFIED;
  // The unified candidate decoder IS the production RPM mapping.
  flags |= BLE_RPM_FLAG_RPM_MAPPED;
  // Bit 3 reports what the LED engine is ACTUALLY rendering from live RPM
  // data (verified bar, provisional preview or the RPM warning overlay). It
  // is deliberately NOT a calibration mirror: effect 5 drives the strip while
  // unverified, and a verified calibration with a decorative effect renders
  // nothing RPM-driven. The synthetic bench test also keeps this bit clear.
  if (rpmLightsRendering) flags |= BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED;
  if (transport.healthy()) flags |= BLE_RPM_FLAG_CAN_HEALTHY;
  if (transport.hasTraffic(nowMs)) flags |= BLE_RPM_FLAG_TRAFFIC_ACTIVE;
  if (recorder.isRecording() && !recorder.isFlashPreserved() && !recorder.isStorageFull()) {
    flags |= BLE_RPM_FLAG_RECORDING_ACTIVE;
  }
  if (recorder.isFlashPreserved()) {
    flags |= BLE_RPM_FLAG_CAPTURE_PRESERVED;
  }
  return flags;
}

void BleManager::sendRpmNotification(uint32_t nowMs,
                                     const RpmCandidateMonitor& candidate,
                                     const CanTransport& transport,
                                     const CanRecorder& recorder,
                                     bool rpmLightsRendering) {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pRpmChar) return;

  BleRpmPacket pkt{};
  pkt.version = PULSESHIFT_BLE_PROTOCOL_VERSION;

  const bool fresh = candidate.isFresh(nowMs);
  pkt.statusFlags = computeRpmStatusFlags(candidate, transport, recorder, nowMs, rpmLightsRendering);

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
  (void)nowMs; (void)candidate; (void)transport; (void)recorder; (void)rpmLightsRendering;
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
    activeMsgFlags_ = liveLogFlags_[liveLogTail_];
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

  uint8_t flags = activeMsgFlags_;
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
    activeMsgFlags_ = 0;
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
    activeMsgFlags_ = 0;
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

#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
namespace {
void formatCaptureRecordLine(const CanLogRecord& rec, char* out, size_t outLen) {
  char hexData[17] = "";
  const uint8_t len = (rec.dlc <= 8) ? rec.dlc : 8;
  for (uint8_t i = 0; i < len; ++i) {
    snprintf(&hexData[i * 2], 3, "%02X", rec.data[i]);
  }
  snprintf(out, outLen, "CAP,%lu,%u,%0*lX,%d,%u,%s\r\n",
           (unsigned long)rec.timestampMs,
           rec.stage,
           (rec.id & 0x80000000u) ? 8 : 3,
           (unsigned long)(rec.id & 0x1FFFFFFFu),
           (rec.id & 0x80000000u) ? 1 : 0,
           rec.dlc,
           hexData);
}
}  // namespace
#endif // NimBLE guard for the capture-line formatter

void BleManager::processCapturePage() {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (!s_pLogsChar) return;

  if (captureCancelRequested_) {
    CanRecorder::instance().bleExportCancel();
    capturePageActive_ = false;
    capturePageEopPending_ = false;
    activeMsgInProgress_ = false;
    captureLineStaged_ = false;
    return;
  }

  // Deferred End-of-Page marker: wait until the active message finished.
  if (capturePageEopPending_) {
    if (activeMsgInProgress_) {
      processLogQueue();
      return;
    }
    BleLogChunkPacket eop{};
    eop.version = PULSESHIFT_BLE_PROTOCOL_VERSION;
    eop.flags = BLE_LOG_FLAG_END_OF_PAGE | BLE_LOG_FLAG_FINAL_CHUNK | BLE_LOG_FLAG_CAPTURE_LINE;
    eop.messageId = logMsgId_++;
    eop.chunkIndex = 0;
    eop.payloadLength = 5;
    memcpy(&eop.payload[0], &captureNextRecord_, 4);
    eop.payload[4] = capturePageFinished_;
    s_pLogsChar->setValue(reinterpret_cast<const uint8_t*>(&eop), sizeof(eop));
    s_pLogsChar->notify();
    ++notifyCount_;
    ++logNotifyCount_;

    capturePageEopPending_ = false;
    capturePageActive_ = false;
    return;
  }

  auto& rec = CanRecorder::instance();
  if (!captureLineStaged_) {
    CanLogRecord record{};
    uint32_t recordIndex = 0;
    if (rec.bleExportNext(record, recordIndex)) {
      char line[96];
      formatCaptureRecordLine(record, line, sizeof(line));
      captureNextRecord_ = recordIndex + 1;

      strncpy(activeMsgBuf_, line, sizeof(activeMsgBuf_) - 1);
      activeMsgBuf_[sizeof(activeMsgBuf_) - 1] = '\0';
      activeMsgLen_ = strlen(activeMsgBuf_);
      activeMsgOffset_ = 0;
      activeChunkIndex_ = 0;
      activeMsgId_ = logMsgId_++;
      activeMsgFlags_ = BLE_LOG_FLAG_CAPTURE_LINE;
      activeMsgInProgress_ = true;
      captureLineStaged_ = true;
      processLogQueue();
      ++captureRecordsSent_;

      if (captureRecordsSent_ >= CanRecorder::BLE_EXPORT_PAGE_RECORDS) {
        capturePageFinished_ = 0;
        capturePageEopPending_ = true;
        capturePageActive_ = false;
        captureLineStaged_ = false;
      }
    } else {
      // End of capture (or unreadable capture).
      capturePageFinished_ = 1;
      capturePageEopPending_ = true;
      capturePageActive_ = false;
      captureLineStaged_ = false;
    }
  }
  (void)rec;
#else
  (void)captureCancelRequested_;
#endif
}

void BleManager::processConfigCommand(uint32_t nowMs, RpmCandidateMonitor& candidate,
                                      const CanTransport& transport, const CanRecorder& recorder) {
  // Pop exactly one queued command per pass (bounded work per loop iteration).
  if (configQueueTail_.load(std::memory_order_acquire) == configQueueHead_.load(std::memory_order_acquire)) {
    return;
  }
  const uint8_t tail = configQueueTail_.load(std::memory_order_relaxed);
  const PendingConfigCmd queued = configQueue_[tail];
  configQueueTail_.store(static_cast<uint8_t>((tail + 1) % CONFIG_QUEUE_CAPACITY), std::memory_order_release);
  const BleConfigCommandPacket pkt = queued.packet;
  const uint16_t connId = queued.connId;

  BleConfigResultPacket res{};
  res.version = PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION;
  res.respondedOpcode = pkt.opcode;
  res.sequence = pkt.sequence;

  auto reject = [&](uint8_t statusCode, uint8_t flags) {
    res.statusCode = statusCode;
    res.resultFlags = flags;
    res.activeRevision = ConfigManager::instance().activeRevision();
    res.payloadLength = 0;
    sendResult(res);
  };

  if (pkt.version != PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION) {
    reject(BLE_CFG_STATUS_VERSION_MISMATCH, 0);
    return;
  }

  // Payload length must fit the 14-byte command payload array; a larger
  // declared length is malformed and can never reference existing arguments.
  if (pkt.payloadLength > sizeof(pkt.payload)) {
    reject(BLE_CFG_STATUS_INVALID_PARAM, 0);
    return;
  }

  if ((pkt.flags & ~BLE_CFG_FLAG_STAGED) != 0) {
    reject(BLE_CFG_STATUS_INVALID_PARAM, 0);
    return;
  }

  const bool isStaged = (pkt.flags & BLE_CFG_FLAG_STAGED) != 0;
  uint8_t status = BLE_CFG_STATUS_OK;
  uint8_t resFlags = 0;

  // Authorization: read-only queries and the auth flow itself bypass the
  // session check; every mutating command requires a session bound to THIS
  // connection (or the physical BOOT commissioning window).
  const bool isReadQuery = (pkt.opcode == BLE_CFG_OP_GET_CAPABILITIES ||
                            pkt.opcode == BLE_CFG_OP_GET_SNAPSHOT ||
                            pkt.opcode == BLE_CFG_OP_GET_DIAGNOSTICS ||
                            pkt.opcode == BLE_CFG_OP_CAL_STATUS ||
                            pkt.opcode == BLE_CFG_OP_CAL_CAPTURE_STATUS);
  const bool isAuthFlow = (pkt.opcode == BLE_CFG_OP_AUTHENTICATE ||
                           pkt.opcode == BLE_CFG_OP_REQUEST_CHALLENGE ||
                           pkt.opcode == BLE_CFG_OP_ENROLL_OWNER);
  if (!isReadQuery && !isAuthFlow) {
    if (!ConfigManager::instance().isAuthorizedFor(connId, nowMs)) {
      reject(BLE_CFG_STATUS_UNAUTHORIZED, BLE_CFG_RESULT_FLAG_AUTH_REQUIRED);
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
      status = ConfigManager::instance().enrollOwner(pkt.payload, pkt.payloadLength, nowMs, connId);
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
                                                               nowMs, connId);
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
            syncDecoderToMonitor(candidate);
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
            syncDecoderToMonitor(candidate);
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

    case BLE_CFG_OP_SET_RPM_HYSTERESIS: {
      if (pkt.payloadLength < 2) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        const uint16_t hyst = static_cast<uint16_t>(pkt.payload[0]) | (static_cast<uint16_t>(pkt.payload[1]) << 8);
        status = ConfigManager::instance().setRpmHysteresis(hyst, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_SET_RPM_DECODER_EXT: {
      if (pkt.payloadLength < 8) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        const uint8_t width = pkt.payload[0];
        const bool littleEndian = (pkt.payload[1] != 0);
        const int32_t offX1000 = static_cast<int32_t>(
            static_cast<uint32_t>(pkt.payload[2]) |
            (static_cast<uint32_t>(pkt.payload[3]) << 8) |
            (static_cast<uint32_t>(pkt.payload[4]) << 16) |
            (static_cast<uint32_t>(pkt.payload[5]) << 24));
        const uint16_t maxValid = static_cast<uint16_t>(pkt.payload[6]) |
                                  (static_cast<uint16_t>(pkt.payload[7]) << 8);
        status = ConfigManager::instance().setRpmDecoderExt(width, littleEndian,
                                                            offX1000 / 1000.0f, maxValid, isStaged);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= isStaged ? BLE_CFG_RESULT_FLAG_STAGED : BLE_CFG_RESULT_FLAG_APPLIED;
          if (!isStaged) {
            syncDecoderToMonitor(candidate);
          }
        }
      }
      break;
    }

    case BLE_CFG_OP_SAVE_CONFIG: {
      const bool ok = ConfigManager::instance().saveToNvs();
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
          // Lighting preferences were reset; the decoder pipeline is rebuilt
          // from the (reset) configuration. Owner credentials are preserved.
          syncDecoderToMonitor(candidate);
        }
      }
      break;
    }

    case BLE_CFG_OP_RELOAD_CONFIG: {
      const bool ok = ConfigManager::instance().reloadFromNvs();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_INVALID_STATE;
      if (ok) {
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        syncDecoderToMonitor(candidate);
      }
      break;
    }

    case BLE_CFG_OP_BEGIN_TRANSACTION: {
      const bool ok = ConfigManager::instance().beginTransaction();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_INVALID_STATE;
      if (ok) resFlags |= BLE_CFG_RESULT_FLAG_STAGED;
      break;
    }

    case BLE_CFG_OP_COMMIT_TRANSACTION: {
      const bool ok = ConfigManager::instance().commitTransaction();
      status = ok ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_VALIDATION_FAILED;
      if (ok) {
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        syncDecoderToMonitor(candidate);
      }
      break;
    }

    case BLE_CFG_OP_ABORT_TRANSACTION: {
      ConfigManager::instance().abortTransaction();
      status = BLE_CFG_STATUS_OK;
      resFlags = 0;
      break;
    }

    // --- Mobile-assisted RPM calibration workflow ---
    case BLE_CFG_OP_CAL_BEGIN: {
      status = ConfigManager::instance().beginCalibrationSession(nowMs)
                   ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_BUSY;
      break;
    }

    case BLE_CFG_OP_CAL_ABORT: {
      ConfigManager::instance().abortCalibrationSession();
      status = BLE_CFG_STATUS_OK;
      break;
    }

    case BLE_CFG_OP_CAL_CAPTURE_START: {
      status = ConfigManager::instance().startCalibrationCapture(nowMs)
                   ? BLE_CFG_STATUS_OK : BLE_CFG_STATUS_INVALID_STATE;
      break;
    }

    case BLE_CFG_OP_CAL_CAPTURE_STATUS: {
      status = BLE_CFG_STATUS_OK;
      const uint16_t samples = ConfigManager::instance().calibrationCaptureSamples();
      res.payload[0] = static_cast<uint8_t>(samples & 0xFF);
      res.payload[1] = static_cast<uint8_t>((samples >> 8) & 0xFF);
      const uint16_t elapsedMs = ConfigManager::instance().calibrationCaptureActive()
          ? static_cast<uint16_t>(nowMs - ConfigManager::instance().calibrationCaptureStartMs())
          : 0;
      res.payload[2] = static_cast<uint8_t>(elapsedMs & 0xFF);
      res.payload[3] = static_cast<uint8_t>((elapsedMs >> 8) & 0xFF);
      res.payloadLength = 4;
      break;
    }

    case BLE_CFG_OP_CAL_POINT_COMMIT: {
      if (pkt.payloadLength < 2) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        const uint16_t refRpm = static_cast<uint16_t>(pkt.payload[0]) |
                                (static_cast<uint16_t>(pkt.payload[1]) << 8);
        status = ConfigManager::instance().commitCalibrationPoint(refRpm, nowMs);
        if (status == BLE_CFG_STATUS_OK) {
          resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        }
      }
      break;
    }

    case BLE_CFG_OP_CAL_COMPUTE: {
      const bool apply = (pkt.payloadLength >= 1 && pkt.payload[0] != 0);
      const CalibrationResult fit = ConfigManager::instance().computeCalibration(apply, nowMs);
      status = fit.status;
      if (status == BLE_CFG_STATUS_OK) {
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        // Payload (exactly 9 bytes): multiplier x1000 (s32 LE), offset x1000
        // (s32 LE), info byte = pointsUsed (low nibble) | worst residual in
        // 10-RPM steps clamped to 15 (high nibble, covers 0..150 RPM).
        const int32_t multX1000 = static_cast<int32_t>(fit.multiplier * 1000.0f);
        const int32_t offX1000 = static_cast<int32_t>(fit.offset * 1000.0f);
        res.payload[0] = static_cast<uint8_t>(multX1000 & 0xFF);
        res.payload[1] = static_cast<uint8_t>((multX1000 >> 8) & 0xFF);
        res.payload[2] = static_cast<uint8_t>((multX1000 >> 16) & 0xFF);
        res.payload[3] = static_cast<uint8_t>((multX1000 >> 24) & 0xFF);
        res.payload[4] = static_cast<uint8_t>(offX1000 & 0xFF);
        res.payload[5] = static_cast<uint8_t>((offX1000 >> 8) & 0xFF);
        res.payload[6] = static_cast<uint8_t>((offX1000 >> 16) & 0xFF);
        res.payload[7] = static_cast<uint8_t>((offX1000 >> 24) & 0xFF);
        const uint8_t residualStep = static_cast<uint8_t>((static_cast<uint32_t>(fit.maxResidualRpm) + 5) / 10);
        res.payload[8] = static_cast<uint8_t>((fit.pointsUsed & 0x0F) |
                                              ((residualStep > 15 ? 15 : residualStep) << 4));
        res.payloadLength = 9;
        if (apply) {
          // The live decoder must adopt the newly bound verification record.
          syncDecoderToMonitor(candidate);
        }
      } else {
        // Failure reason string (bounded, NUL-terminated).
        const size_t reasonLen = strlen(fit.reason);
        const size_t n = (reasonLen > 9) ? 9 : reasonLen;
        memcpy(res.payload, fit.reason, n);
        res.payloadLength = static_cast<uint8_t>(n);
      }
      break;
    }

    case BLE_CFG_OP_CAL_STATUS: {
      status = BLE_CFG_STATUS_OK;
      res.payload[0] = ConfigManager::instance().isCalibrationSessionActive() ? 1 : 0;
      res.payload[1] = ConfigManager::instance().calibrationPointCount();
      res.payload[2] = ConfigManager::instance().calibrationCaptureActive() ? 1 : 0;
      const uint16_t samples = ConfigManager::instance().calibrationCaptureSamples();
      res.payload[3] = static_cast<uint8_t>(samples & 0xFF);
      res.payload[4] = static_cast<uint8_t>((samples >> 8) & 0xFF);
      res.payloadLength = 5;
      break;
    }

    // --- Device control & diagnostics ---
    case BLE_CFG_OP_GET_DIAGNOSTICS: {
      status = BLE_CFG_STATUS_OK;
      resFlags = 0;
      snapshotLen_ = buildDiagnosticsBlob(snapshotBuf_, sizeof(snapshotBuf_), nowMs,
                                          candidate, transport, recorder);
      snapshotId_ = pkt.sequence;
      snapshotTotalChunks_ = static_cast<uint8_t>((snapshotLen_ + 13) / 14);
      snapshotChunkIdx_ = 0;
      snapshotActive_ = true;
      break;
    }

    case BLE_CFG_OP_RESTART_DEVICE: {
      if (pkt.payloadLength < 2 || pkt.payload[0] != 0xA5 || pkt.payload[1] != 0x5A) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
        break;
      }
      DiagLogger::instance().logEvent(DiagLevel::Info, "BLE_RESTART",
                                      "Restart requested by authenticated owner; restarting now.");
      DiagLogger::instance().flush();
      // Acknowledge first, give the notification a moment to reach the radio
      // buffer, then reset the ESP32.
      res.statusCode = BLE_CFG_STATUS_OK;
      res.resultFlags = BLE_CFG_RESULT_FLAG_APPLIED;
      res.activeRevision = ConfigManager::instance().activeRevision();
      sendResult(res);
#if defined(ARDUINO)
      delay(150);
      esp_restart();
#else
      status = BLE_CFG_STATUS_OK;
      resFlags = BLE_CFG_RESULT_FLAG_APPLIED;
#endif
      break;
    }

    case BLE_CFG_OP_CLEAR_DIAGNOSTICS: {
      if (pkt.payloadLength < 2 || pkt.payload[0] != 0xA5 || pkt.payload[1] != 0x5A) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
        break;
      }
      // Clears the diagnostic event log ONLY. Stored CAN captures and owner
      // credentials are never touched by this command.
      DiagLogger::instance().clear();
      status = BLE_CFG_STATUS_OK;
      resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
      break;
    }

    case BLE_CFG_OP_SET_CAPTURE_RECORDING: {
      if (pkt.payloadLength < 1) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
        break;
      }
      auto& rec = CanRecorder::instance();
      if (pkt.payload[0] != 0) {
        if (rec.isDamagedRecording() || rec.isFlashPreserved() || rec.isStorageFull() ||
            !StorageManager::instance().isMounted() || StorageManager::instance().mountFailed()) {
          status = BLE_CFG_STATUS_INVALID_STATE;
        } else {
          rec.setRecording(true);
          status = BLE_CFG_STATUS_OK;
          resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
        }
      } else {
        rec.flush();
        rec.setRecording(false);
        status = BLE_CFG_STATUS_OK;
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
      }
      break;
    }

    case BLE_CFG_OP_SET_CAPTURE_STAGE: {
      if (pkt.payloadLength != 1 || pkt.payload[0] < 1 || pkt.payload[0] > 3) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        CanRecorder::instance().setStage(static_cast<CanRecorder::Stage>(pkt.payload[0]));
        status = BLE_CFG_STATUS_OK;
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
      }
      break;
    }

    case BLE_CFG_OP_SET_LED_TEST_RPM: {
      if (pkt.payloadLength != 3 ||
          (pkt.payload[2] != 0 && pkt.payload[2] != 1)) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
        break;
      }
      const uint16_t rpm = static_cast<uint16_t>(pkt.payload[0]) |
                           (static_cast<uint16_t>(pkt.payload[1]) << 8);
      if (rpm > 20000) {
        status = BLE_CFG_STATUS_INVALID_PARAM;
      } else {
        benchTestRpm_ = rpm;
        benchTestActive_ = pkt.payload[2] != 0;
        benchTestPending_ = true;
        status = BLE_CFG_STATUS_OK;
        resFlags |= BLE_CFG_RESULT_FLAG_APPLIED;
      }
      break;
    }

    default:
      status = BLE_CFG_STATUS_UNSUPPORTED;
      break;
  }

  res.statusCode = status;
  res.resultFlags = resFlags;
  res.activeRevision = ConfigManager::instance().activeRevision();
  sendResult(res);
  updateCapabilitiesChar();
}

void BleManager::sendResult(BleConfigResultPacket& res) {
#if defined(ARDUINO) && defined(PULSESHIFT_BLE_ENABLED) && (PULSESHIFT_BLE_ENABLED == 1)
  if (s_pConfigResultChar) {
    s_pConfigResultChar->setValue(reinterpret_cast<const uint8_t*>(&res), sizeof(res));
    if (isConfigResultSubscribed()) {
      s_pConfigResultChar->notify();
      ++notifyCount_;
      ++configResultNotifyCount_;
    }
  }
#else
#if defined(PULSESHIFT_TEST_BUILD)
  if (testResultCount_ < sizeof(testResults_) / sizeof(testResults_[0])) {
    testResults_[testResultCount_++] = res;
  }
#endif
  (void)res;
#endif
}

#if defined(PULSESHIFT_TEST_BUILD)
void BleManager::testEnqueueCommand(const BleConfigCommandPacket& pkt, uint16_t connId) {
  const uint8_t head = configQueueHead_.load(std::memory_order_relaxed);
  const uint8_t next = static_cast<uint8_t>((head + 1) % CONFIG_QUEUE_CAPACITY);
  if (next != configQueueTail_.load(std::memory_order_acquire)) {
    configQueue_[head].packet = pkt;
    configQueue_[head].connId = connId;
    configQueueHead_.store(next, std::memory_order_release);
  }
}

void BleManager::testEnqueueCtrlCommand(uint8_t opcode, uint32_t offset, uint16_t connId) {
  const uint8_t head = ctrlQueueHead_.load(std::memory_order_relaxed);
  const uint8_t next = static_cast<uint8_t>((head + 1) % CTRL_QUEUE_CAPACITY);
  if (next != ctrlQueueTail_.load(std::memory_order_acquire)) {
    ctrlQueue_[head].opcode = opcode;
    ctrlQueue_[head].offset = offset;
    ctrlQueue_[head].connId = connId;
    ctrlQueueHead_.store(next, std::memory_order_release);
  }
}
#endif

void BleManager::syncDecoderToMonitor(RpmCandidateMonitor& candidate) {
  // Rebuild the authoritative live decoder from the active ConfigManager
  // record (single source of truth) and refresh the calibration verification
  // mirror. The verification mirror is only kept when the stored calibration
  // fingerprint still matches the exact decoder configuration being applied.
  const PulseShiftConfig& cfg = ConfigManager::instance().active();
  RpmDecoderSettings s;
  s.canId = cfg.canCandidateId;
  s.extended = cfg.isExtendedFrame;
  s.byteIndex = cfg.candidateByteIndex;
  s.byteWidth = cfg.candidateByteWidth;
  s.littleEndian = cfg.candidateLittleEndian;
  s.multiplier = cfg.rpmMultiplier;
  s.offset = cfg.rpmOffset;
  s.maxRpm = cfg.rpmMaxValid;
  candidate.applyDecoderSettings(s, millis());
  candidate.setCalibrationVerified(cfg.calibrationVerified &&
                                   cfg.calFingerprint == computeDecoderFingerprint(s));
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

size_t BleManager::buildDiagnosticsBlob(uint8_t* outBuf, size_t maxLen, uint32_t nowMs,
                                        const RpmCandidateMonitor& candidate,
                                        const CanTransport& transport,
                                        const CanRecorder& recorder) const {
  if (!outBuf || maxLen < 64) return 0;

  size_t offset = 0;
  outBuf[offset++] = 1; // Diagnostics blob schema version

  // Revision field (u32 LE, 0 for diagnostics blobs)
  outBuf[offset++] = 0;
  outBuf[offset++] = 0;
  outBuf[offset++] = 0;
  outBuf[offset++] = 0;

  auto appendTlv = [&](uint8_t id, const uint8_t* val, uint8_t len) {
    if (offset + 2 + len > maxLen - 4) return;
    outBuf[offset++] = id;
    outBuf[offset++] = len;
    memcpy(&outBuf[offset], val, len);
    offset += len;
  };

  auto putU16 = [](uint8_t* p, uint16_t v) { p[0] = uint8_t(v & 0xFF); p[1] = uint8_t((v >> 8) & 0xFF); };
  auto putU32 = [](uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v & 0xFF); p[1] = uint8_t((v >> 8) & 0xFF);
    p[2] = uint8_t((v >> 16) & 0xFF); p[3] = uint8_t((v >> 24) & 0xFF);
  };

  auto& sm = StorageManager::instance();
  auto& diag = DiagLogger::instance();
  auto& cfgMgr = ConfigManager::instance();

  // TLV 0x40: uptime
  {
    uint8_t v[4];
    putU32(v, nowMs);
    appendTlv(BLE_DIAG_TLV_UPTIME, v, 4);
  }

  // TLV 0x41: firmware version
  {
    const uint8_t v[3] = {PULSESHIFT_FW_VERSION_MAJOR, PULSESHIFT_FW_VERSION_MINOR, PULSESHIFT_FW_VERSION_PATCH};
    appendTlv(BLE_DIAG_TLV_FIRMWARE, v, 3);
  }

#if defined(ARDUINO)
  // TLV 0x42: reset reason of the last boot
  {
    const uint8_t v = static_cast<uint8_t>(esp_reset_reason());
    appendTlv(BLE_DIAG_TLV_RESET_REASON, &v, 1);
  }
#endif

  // TLV 0x43: CAN controller status
  {
    uint8_t v[26];
    putU32(&v[0], transport.bitrate());
    putU32(&v[4], transport.totalRx());
    putU32(&v[8], transport.acceptedFrames());
    putU32(&v[12], transport.missedFrames());
    putU32(&v[16], transport.overrunFrames());
    putU32(&v[20], transport.busErrors());
    v[24] = transport.healthy() ? 1 : 0;
    v[25] = transport.hasTraffic(nowMs) ? 1 : 0;
    appendTlv(BLE_DIAG_TLV_CAN_STATUS, v, 26);
  }

  // TLV 0x44: unified RPM candidate decoder state (34 bytes)
  {
    uint8_t v[34];
    putU32(&v[0], candidate.candidateId());
    v[4] = candidate.byteIndex();
    v[5] = candidate.byteWidth();
    v[6] = candidate.isExtended() ? 1 : 0;
    putU32(&v[7], static_cast<uint32_t>(static_cast<int32_t>(candidate.multiplier() * 1000.0f)));
    putU32(&v[11], static_cast<uint32_t>(static_cast<int32_t>(candidate.offset() * 1000.0f)));
    putU16(&v[15], candidate.maxRpm());
    putU16(&v[17], candidate.rawValue16());
    putU16(&v[19], candidate.isFresh(nowMs) ? candidate.estimatedRpm(nowMs) : 0);
    v[21] = candidate.isFresh(nowMs) ? 1 : 0;
    v[22] = (candidate.isFresh(nowMs) && candidate.estimatedRpm(nowMs) <= candidate.maxRpm()) ? 1 : 0;
    v[23] = candidate.isCalibrated() ? 1 : 0;
    const uint32_t age = candidate.receivedCount() > 0 ? candidate.lastUpdateAgeMs(nowMs) : 0xFFFFFFFFu;
    putU16(&v[24], (age <= 65534) ? static_cast<uint16_t>(age) : 65534);
    putU32(&v[26], candidate.receivedCount());
    putU32(&v[30], candidate.rejectedCount());
    appendTlv(BLE_DIAG_TLV_CANDIDATE, v, 34);
  }

  // TLV 0x45: storage state (29 bytes)
  {
    uint8_t v[29];
    putU32(&v[0], sm.totalBytes());
    putU32(&v[4], sm.usedBytes());
    putU32(&v[8], sm.freeBytes());
    v[12] = sm.isMounted() ? 1 : 0;
    v[13] = (sm.isStorageFull() || recorder.isStorageFull()) ? 1 : 0;
    v[14] = recorder.isFlashPreserved() ? 1 : 0;
    v[15] = recorder.isDamagedRecording() ? 1 : 0;
    v[16] = recorder.isRecording() ? 1 : 0;
    putU32(&v[17], recorder.recordedFrames());
    putU32(&v[21], static_cast<uint32_t>(recorder.currentFileSize()));
    putU32(&v[25], static_cast<uint32_t>(sm.size(DiagLogger::LOG_PATH)));
    appendTlv(BLE_DIAG_TLV_STORAGE, v, 29);
  }

  // TLV 0x46: BLE state
  {
    const uint8_t v[5] = {
      static_cast<uint8_t>(isClientConnected() ? 1 : 0),
      static_cast<uint8_t>(isAdvertising() ? 1 : 0),
      static_cast<uint8_t>(cfgMgr.securityMode()),
      static_cast<uint8_t>(cfgMgr.isOwnerEnrolled() ? 1 : 0),
      static_cast<uint8_t>(cfgMgr.isSessionAuthorized() ? 1 : 0)
    };
    appendTlv(BLE_DIAG_TLV_BLE_STATE, v, 5);
  }

  // TLV 0x47: capture session info
  {
    uint8_t v[9];
    v[0] = static_cast<uint8_t>(recorder.currentStage());
    putU32(&v[1], sm.currentSessionId());
    putU32(&v[5], sm.bootCount());
    appendTlv(BLE_DIAG_TLV_CAPTURE_INFO, v, 9);
  }

  // TLV 0x48: last error message (this session)
  {
    const char* lastErr = diag.lastError();
    const size_t len = lastErr ? strlen(lastErr) : 0;
    if (len > 0) {
      appendTlv(BLE_DIAG_TLV_LAST_ERROR, reinterpret_cast<const uint8_t*>(lastErr),
                static_cast<uint8_t>(len > 96 ? 96 : len));
    }
  }

  // TLV 0x49: calibration state
  {
    uint8_t v[13];
    v[0] = cfgMgr.isCalibrationSessionActive() ? 1 : 0;
    v[1] = cfgMgr.calibrationPointCount();
    v[2] = cfgMgr.calibrationCaptureActive() ? 1 : 0;
    putU16(&v[3], cfgMgr.calibrationCaptureSamples());
    v[5] = cfgMgr.active().calibrationVerified ? 1 : 0;
    v[6] = cfgMgr.active().calPointsUsed;
    v[7] = cfgMgr.active().calMaxResidualRpm;
    putU32(&v[8], cfgMgr.active().calFingerprint);
    appendTlv(BLE_DIAG_TLV_CALIBRATION, v, 13);
  }

  // Trailing CRC32 over everything serialized so far
  const uint32_t crc = ConfigManager::computeCrc32(outBuf, offset);
  outBuf[offset++] = static_cast<uint8_t>(crc & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  outBuf[offset++] = static_cast<uint8_t>((crc >> 24) & 0xFF);

  return offset;
}
