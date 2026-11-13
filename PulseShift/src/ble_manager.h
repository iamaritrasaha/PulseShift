// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "ble_protocol.h"
#include "rpm_candidate.h"
#include "can_transport.h"
#include "can_recorder.h"
#include "rpm_decoder.h"
#include <stdint.h>
#include <stddef.h>
#include <atomic>

class BleManager {
 public:
  static BleManager& instance();

  bool begin();
  void poll(uint32_t nowMs,
            RpmCandidateMonitor& candidate,
            const CanTransport& transport,
            const CanRecorder& recorder,
            const RpmState& rpm);

  bool isClientConnected() const;
  bool isRpmSubscribed() const;
  bool isHealthSubscribed() const;
  bool isLogsSubscribed() const;
  bool isConfigResultSubscribed() const;
  bool isConfigSnapshotSubscribed() const;
  bool isEnabled() const { return initialized_; }
  bool isAdvertising() const;

  // Called when client disconnects to cleanly reset log retrieval state
  void onClientDisconnected();

  // Enqueue a live diagnostic log event to be broadcast over BLE (if subscribed)
  void queueLiveLog(const char* text);

  // Status diagnostics
  uint32_t notifyCount() const { return notifyCount_; }
  uint32_t dropCount() const { return dropCount_; }
  uint32_t rpmNotifyCount() const { return rpmNotifyCount_; }
  uint32_t healthNotifyCount() const { return healthNotifyCount_; }
  uint32_t logNotifyCount() const { return logNotifyCount_; }
  void printStatus() const;

  // Rebuild the authoritative live decoder from the active configuration and
  // refresh the calibration mirror (single source of truth: ConfigManager).
  // Called by the application on boot, after config commits and serial overrides.
  static void syncDecoderToMonitor(RpmCandidateMonitor& candidate);

 private:
  BleManager();
  ~BleManager() = default;

  void sendRpmNotification(uint32_t nowMs,
                           const RpmCandidateMonitor& candidate,
                           const CanTransport& transport,
                           const CanRecorder& recorder,
                           const RpmState& rpm);

  void sendHealthNotification(const CanTransport& transport,
                              const CanRecorder& recorder);

  void processLogQueue();
  void processHistoricalPageRead();
  void processCapturePage();
  void processConfigCommand(uint32_t nowMs, RpmCandidateMonitor& candidate);
  void processSnapshotStream();
  void updateCapabilitiesChar();
  void sendResult(BleConfigResultPacket& res);
  size_t buildDiagnosticsBlob(uint8_t* outBuf, size_t maxLen,
                              const RpmCandidateMonitor& candidate,
                              const CanTransport& transport,
                              const CanRecorder& recorder) const;

  // Internal state
  bool initialized_ = false;
  uint16_t rpmSeq_ = 0;
  uint16_t healthSeq_ = 0;
  uint16_t logMsgId_ = 0;

  uint32_t lastRpmNotifyMs_ = 0;
  uint32_t lastHealthNotifyMs_ = 0;
  uint32_t lastLogNotifyMs_ = 0;
  static constexpr uint32_t LOG_NOTIFICATION_INTERVAL_MS = 20; // 20 ms pacing between log chunks
  uint32_t notifyCount_ = 0;
  uint32_t dropCount_ = 0;
  uint32_t rpmNotifyCount_ = 0;
  uint32_t healthNotifyCount_ = 0;
  uint32_t logNotifyCount_ = 0;
  uint32_t configResultNotifyCount_ = 0;
  uint32_t configSnapshotNotifyCount_ = 0;

  // Bounded ring buffer for live log events (16 messages, max 128 chars each)
  static constexpr size_t LIVE_LOG_CAPACITY = 16;
  static constexpr size_t LIVE_LOG_MAX_LEN = 128;
  char liveLogRing_[LIVE_LOG_CAPACITY][LIVE_LOG_MAX_LEN]{};
  uint8_t liveLogHead_ = 0;
  uint8_t liveLogTail_ = 0;
  uint8_t liveLogCount_ = 0;

  // Active message chunking state
  bool activeMsgInProgress_ = false;
  uint16_t activeMsgId_ = 0;
  char activeMsgBuf_[LIVE_LOG_MAX_LEN]{};
  size_t activeMsgLen_ = 0;
  size_t activeMsgOffset_ = 0;
  uint8_t activeChunkIndex_ = 0;

  // CAN capture page streaming state (v1 control extension)
  bool capturePageActive_ = false;
  bool capturePageEopPending_ = false;
  uint8_t capturePageFinished_ = 0;
  uint32_t captureNextRecord_ = 0;
  uint8_t captureRecordsSent_ = 0;
  bool captureLineStaged_ = false;
  bool captureCancelRequested_ = false;
  uint32_t captureCorruptSeen_ = 0;

  // Control request command queue (thread-safe decoupling from NimBLE callback)
  struct PendingCtrlCmd {
    uint8_t opcode = 0;
    uint32_t offset = 0;
    std::atomic<bool> pending{false};
  };
  PendingCtrlCmd pendingCtrlCmd_{};
  bool histPageActive_ = false;
  bool histPageEopPending_ = false;
  uint8_t histPageFinished_ = 0;
  uint32_t histFileOffset_ = 0;
  uint8_t histLinesSent_ = 0;
  bool histFinished_ = false;
  bool histCancelRequested_ = false;

  // Configuration V2 Command Queue
  struct PendingConfigCmd {
    BleConfigCommandPacket packet{};
    uint16_t connId = 0;
    std::atomic<bool> pending{false};
  };
  PendingConfigCmd pendingConfigCmd_{};

  // Configuration Snapshot Stream State
  bool snapshotActive_ = false;
  uint16_t snapshotId_ = 0;
  uint8_t snapshotBuf_[512]{};
  size_t snapshotLen_ = 0;
  uint8_t snapshotChunkIdx_ = 0;
  uint8_t snapshotTotalChunks_ = 0;

  friend class BleServerCallbacksImpl;
  friend class BleControlCallbacksImpl;
  friend class BleRpmCallbacksImpl;
  friend class BleHealthCallbacksImpl;
  friend class BleLogCallbacksImpl;
  friend class BleConfigCmdCallbacksImpl;
  friend class BleConfigResultCallbacksImpl;
  friend class BleConfigSnapshotCallbacksImpl;
};
