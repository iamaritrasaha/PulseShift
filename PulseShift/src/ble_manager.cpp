// Copyright (c) 2026 Aritra Saha. All rights reserved.
#if defined(ARDUINO)
  #include <Arduino.h>
#endif

#include "config.h"
#include "ble_manager.h"
#include "storage_manager.h"
#include "diag_logger.h"
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

static std::atomic<bool> s_clientConnected{false};
static std::atomic<bool> s_rpmSubscribed{false};
static std::atomic<bool> s_healthSubscribed{false};
static std::atomic<bool> s_logsSubscribed{false};

class BleServerCallbacksImpl : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    s_clientConnected.store(true, std::memory_order_release);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Client connected: %s\r\n", connInfo.getAddress().toString().c_str());
#endif
    // Request balanced connection parameters: min 20ms, max 40ms, latency 0, timeout 2000ms
    pServer->updateConnParams(connInfo.getConnHandle(), 16, 32, 0, 200);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer; (void)connInfo; (void)reason;
    s_clientConnected.store(false, std::memory_order_release);
    s_rpmSubscribed.store(false, std::memory_order_release);
    s_healthSubscribed.store(false, std::memory_order_release);
    s_logsSubscribed.store(false, std::memory_order_release);
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
      }
    }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("[BLE] Rejected unauthorized / invalid control command (len=%u)\r\n", (unsigned)len);
#endif
  }
};

static BleServerCallbacksImpl s_serverCallbacks;
static BleRpmCallbacksImpl s_rpmCallbacks;
static BleHealthCallbacksImpl s_healthCallbacks;
static BleLogCallbacksImpl s_logCallbacks;
static BleControlCallbacksImpl s_controlCallbacks;

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
  Serial.println("Protocol: PulseShift BLE v1 (fixed 20-byte packed frames, little-endian)");
  Serial.println("========================================");
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
                      const RpmCandidateMonitor& candidate,
                      const CanTransport& transport,
                      const CanRecorder& recorder,
                      const RpmState& rpm) {
  if (!initialized_) return;

  if (!isClientConnected()) {
    if (histPageActive_ || activeMsgInProgress_) {
      onClientDisconnected();
    }
    return;
  }

  // 0. Handle any decoupled control commands received via BLE write callbacks
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

  // 3. Service diagnostic log chunks with strict time-based pacing (20 ms between consecutive notifications)
  if (isLogsSubscribed() && (uint32_t(nowMs - lastLogNotifyMs_) >= LOG_NOTIFICATION_INTERVAL_MS)) {
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
  // Calibration remains UNVERIFIED in v1; never set verified flag
  if (rpm.mapped()) flags |= BLE_RPM_FLAG_RPM_MAPPED;
  // Physical shift lights remain DISABLED for unverified candidate; never set shift-lights flag
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
