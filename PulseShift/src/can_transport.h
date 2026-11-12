// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "config.h"
#include "frame_log.h"
#include "can_frame.h"
#include "tx_tracker.h"
#include "event_summary.h"
#include <driver/twai.h>

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
  bool healthy() const { return running_ && !tracker_.halted(); }
  bool configured() const { return bitrate_ != 0; }
  uint32_t bitrate() const { return bitrate_; }
  bool hasTraffic(uint32_t now) const { return rx_ > 0 && uint32_t(now - lastRxMs_) < 1000; }
  uint32_t lastRxMs() const { return lastRxMs_; }
  uint32_t missedFrames() const { return lastMissedCount_; }
  uint32_t overrunFrames() const { return lastOverrunCount_; }
  uint32_t busErrors() const { return lastBusErrorCount_; }
  uint32_t busErrorAlerts() const { return busErrorAlerts_.total(); }
  uint32_t rxOverflowAlerts() const { return rxOverflowAlerts_.total(); }

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  bool sendTest(const CanFrame& frame, uint32_t now);
  bool canSend() const { return healthy() && !tracker_.pending(); }
#endif

 private:
  void fault(const char* reason);
  bool installed_ = false, running_ = false, beginAttempted_ = false;
  uint32_t bitrate_ = 0, acceptedRx_ = 0;
  FrameLog* log_ = nullptr;
  TxTracker tracker_;
  EventSummary busErrorAlerts_;
  EventSummary rxOverflowAlerts_;
  uint32_t queued_ = 0, completed_ = 0, rx_ = 0, matched_ = 0, alertsSeen_ = 0;
  uint32_t lastMissedCount_ = 0, lastOverrunCount_ = 0, lastBusErrorCount_ = 0;
  uint32_t lastRxMs_ = 0;
  bool wasActive_ = false;
};
