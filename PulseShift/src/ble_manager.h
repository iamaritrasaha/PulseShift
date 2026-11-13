// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "ble_protocol.h"
#include "rpm_candidate.h"
#include "can_transport.h"
#include "can_recorder.h"
#include <stdint.h>
#include <stddef.h>
#include <atomic>

// ============================================================================
// BLE GATT server (NimBLE).
//
// Threading contract: NimBLE callbacks run in the NimBLE host task and ONLY
// copy bounded packets into a small SPSC command ring; every protocol action
// (dispatch, notifications, streaming) happens single-threaded in poll() on
// the main loop. Session authorization is bound to the connection handle that
// authenticated, and is re-checked per command.
// ============================================================================

class BleManager {
 public:
  static BleManager& instance();

  bool begin();
  void poll(uint32_t nowMs,
            RpmCandidateMonitor& candidate,
            const CanTransport& transport,
            const CanRecorder& recorder);

  bool isClientConnected() const;
  bool isRpmSubscribed() const;
  bool isHealthSubscribed() const;
  bool isLogsSubscribed() const;
  bool isConfigResultSubscribed() const;
  bool isConfigSnapshotSubscribed() const;
  bool isEnabled() const { return initialized_; }
  bool isAdvertising() const;

  // Called by Application::tick() on the same loop task after poll(). A BLE
  // command may request a 10s LED bench test without granting the BLE layer
  // hardware ownership or bypassing the unified LED controller.
  bool takeBenchTestRequest(uint16_t& rpm, bool& active) {
    if (!benchTestPending_) return false;
    rpm = benchTestRpm_;
    active = benchTestActive_;
    benchTestPending_ = false;
    return true;
  }

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
  uint32_t droppedCommandCount() const { return droppedCommands_; }
  void printStatus() const;

  // Rebuild the authoritative live decoder from the active configuration and
  // refresh the calibration mirror (single source of truth: ConfigManager).
  // Called by the application on boot, after config commits and serial overrides.
  static void syncDecoderToMonitor(RpmCandidateMonitor& candidate);

#if defined(PULSESHIFT_TEST_BUILD)
  // Host-test hooks: inject commands into the same bounded queue the NimBLE
  // callback fills, drain them through the real dispatcher, and read back the
  // result packets captured by the host (non-NimBLE) sendResult path.
  void testEnqueueCommand(const BleConfigCommandPacket& pkt, uint16_t connId);
  void testDrainCommands(uint32_t nowMs, RpmCandidateMonitor& candidate,
                         const CanTransport& transport, const CanRecorder& recorder) {
    while (configQueueTail_.load(std::memory_order_acquire) != configQueueHead_.load(std::memory_order_acquire)) {
      processConfigCommand(nowMs, candidate, transport, recorder);
    }
  }
  size_t testResultCount() const { return testResultCount_; }
  const BleConfigResultPacket& testResult(size_t index) const { return testResults_[index]; }
  size_t testPendingSnapshotLen() const { return snapshotLen_; }
  const uint8_t* testPendingSnapshotData() const { return snapshotBuf_; }
#endif

 private:
  BleManager();
  ~BleManager() = default;

  void sendRpmNotification(uint32_t nowMs,
                           const RpmCandidateMonitor& candidate,
                           const CanTransport& transport,
                           const CanRecorder& recorder);

  void sendHealthNotification(const CanTransport& transport,
                              const CanRecorder& recorder);

  void processLogQueue();
  void processHistoricalPageRead();
  void processCapturePage();
  void processConfigCommand(uint32_t nowMs, RpmCandidateMonitor& candidate,
                            const CanTransport& transport, const CanRecorder& recorder);
  void processSnapshotStream();
  void updateCapabilitiesChar();
  void sendResult(BleConfigResultPacket& res);
  size_t buildDiagnosticsBlob(uint8_t* outBuf, size_t maxLen, uint32_t nowMs,
                              const RpmCandidateMonitor& candidate,
                              const CanTransport& transport,
                              const CanRecorder& recorder) const;

  // Internal state
  bool initialized_ = false;
  bool benchTestPending_ = false;
  bool benchTestActive_ = false;
  uint16_t benchTestRpm_ = 0;
  uint16_t rpmSeq_ = 0;
  uint16_t healthSeq_ = 0;
  uint16_t logMsgId_ = 0;
  uint32_t droppedCommands_ = 0;

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
  uint8_t activeMsgFlags_ = 0; // BleLogFlags for the active message (e.g. CAPTURE_LINE)

  // CAN capture page streaming state (v1 control extension)
  bool capturePageActive_ = false;
  bool capturePageEopPending_ = false;
  uint8_t capturePageFinished_ = 0;
  uint32_t captureNextRecord_ = 0;
  uint8_t captureRecordsSent_ = 0;
  bool captureLineStaged_ = false;
  bool captureCancelRequested_ = false;

  // Control request command queue (thread-safe decoupling from NimBLE callback)
  struct PendingCtrlCmd {
    uint8_t opcode = 0;
    uint32_t offset = 0;
  };
  static constexpr size_t CTRL_QUEUE_CAPACITY = 4;
  PendingCtrlCmd ctrlQueue_[CTRL_QUEUE_CAPACITY]{};
  std::atomic<uint8_t> ctrlQueueHead_{0};  // NimBLE callback task writes
  std::atomic<uint8_t> ctrlQueueTail_{0};  // main loop reads

  bool histPageActive_ = false;
  bool histPageEopPending_ = false;
  uint8_t histPageFinished_ = 0;
  uint32_t histFileOffset_ = 0;
  uint8_t histLinesSent_ = 0;
  bool histFinished_ = false;
  bool histCancelRequested_ = false;

  // Configuration V2 Command Queue (bounded SPSC ring; overflow drops the
  // oldest unprocessed command and is counted for diagnostics).
  struct PendingConfigCmd {
    BleConfigCommandPacket packet{};
    uint16_t connId = 0;
  };
  static constexpr size_t CONFIG_QUEUE_CAPACITY = 4;
  PendingConfigCmd configQueue_[CONFIG_QUEUE_CAPACITY]{};
  std::atomic<uint8_t> configQueueHead_{0}; // NimBLE callback task writes
  std::atomic<uint8_t> configQueueTail_{0}; // main loop reads

  // Configuration Snapshot Stream State (also used for diagnostics blobs)
  bool snapshotActive_ = false;
  uint16_t snapshotId_ = 0;
  uint8_t snapshotBuf_[512]{};
  size_t snapshotLen_ = 0;
  uint8_t snapshotChunkIdx_ = 0;
  uint8_t snapshotTotalChunks_ = 0;

#if defined(PULSESHIFT_TEST_BUILD)
  // Host-only capture of emitted result packets (NimBLE sendResult is inert
  // without the radio stack). Sized for a full dispatch test sequence.
  BleConfigResultPacket testResults_[64]{};
  size_t testResultCount_ = 0;
#endif

  friend class BleServerCallbacksImpl;
  friend class BleControlCallbacksImpl;
  friend class BleRpmCallbacksImpl;
  friend class BleHealthCallbacksImpl;
  friend class BleLogCallbacksImpl;
  friend class BleConfigCmdCallbacksImpl;
  friend class BleConfigResultCallbacksImpl;
  friend class BleConfigSnapshotCallbacksImpl;
};
