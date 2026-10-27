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
  explicit CanTransport(uint32_t bitrate = REVIX_BITRATE, FrameLog* log = nullptr) : bitrate_(bitrate), log_(log) {}
  bool begin();
  void poll(uint32_t now);
  bool receive(CanFrame& frame, uint32_t now);
  void diagnostics() const;
  uint32_t acceptedFrames() const { return acceptedRx_; }
  bool healthy() const { return running_ && !tracker_.halted(); }
  bool configured() const { return bitrate_ != 0; }
#if REVIX_MODE == REVIX_TWAI_TEST
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
  uint32_t queued_ = 0, completed_ = 0, rx_ = 0, matched_ = 0, alertsSeen_ = 0;
};
