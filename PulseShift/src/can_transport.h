// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "config.h"
#include "frame_log.h"
#include "can_frame.h"
#include "event_summary.h"
#include <driver/twai.h>

// Passive TWAI wrapper: LISTEN-ONLY mode, zero-length TX queue, TX pin reset
// to input with pull-up after install. There is no send API at all: the class
// cannot transmit a frame, and the physical build air-gaps GPIO17 besides.
class CanTransport {
 public:
  explicit CanTransport(uint32_t bitrate = PULSESHIFT_BITRATE, FrameLog* log = nullptr)
      : bitrate_(bitrate), log_(log) {}
  bool begin();
  void poll(uint32_t now);
  bool receive(CanFrame& frame, uint32_t now);
  void diagnostics() const;
  uint32_t acceptedFrames() const { return acceptedRx_; }
  uint32_t totalRx() const { return rx_; }
  bool healthy() const { return running_; }
  bool configured() const { return bitrate_ != 0; }
  uint32_t bitrate() const { return bitrate_; }
  bool hasTraffic(uint32_t now) const { return rx_ > 0 && uint32_t(now - lastRxMs_) < 1000; }
  uint32_t lastRxMs() const { return lastRxMs_; }
  uint32_t missedFrames() const { return lastMissedCount_; }
  uint32_t overrunFrames() const { return lastOverrunCount_; }
  uint32_t busErrors() const { return lastBusErrorCount_; }
  uint32_t busErrorAlerts() const { return busErrorAlerts_.total(); }
  uint32_t rxOverflowAlerts() const { return rxOverflowAlerts_.total(); }

 private:
  void fault(const char* reason);
  bool attemptBusOffRecovery(uint32_t now);
  bool installed_ = false, running_ = false, beginAttempted_ = false;
  uint32_t bitrate_ = 0, acceptedRx_ = 0;
  FrameLog* log_ = nullptr;
  EventSummary busErrorAlerts_;
  EventSummary rxOverflowAlerts_;
  uint32_t rx_ = 0, alertsSeen_ = 0;
  uint32_t lastMissedCount_ = 0, lastOverrunCount_ = 0, lastBusErrorCount_ = 0;
  uint32_t lastRxMs_ = 0;
  bool wasActive_ = false;

  // Bounded bus-off auto-recovery. A transient bus-off (e.g. a cranking
  // voltage dip on the transceiver) used to leave the shift light dead until
  // a manual power cycle; in listen-only mode re-entering the bus is safe —
  // the controller cannot disturb it, and the verified physical TX air-gap
  // (GPIO17 disconnected, CTX tied to 3V3) is untouched by recovery.
  static constexpr uint8_t MAX_BUSOFF_RECOVERY_ATTEMPTS = 5;
  bool busOffRecoveryPending_ = false;
  uint32_t lastRecoveryAttemptMs_ = 0;
  uint8_t busOffRecoveryAttempts_ = 0;
};
