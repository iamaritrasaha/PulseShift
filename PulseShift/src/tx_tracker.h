// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "can_frame.h"
#include <string.h>
// Single outstanding transaction: alerts are bitmasks, not counting semaphores.
class TxTracker {
 public:
  bool begin(const CanFrame& f, uint32_t now) {
    if (halted_ || pending_) return false;
    expected_ = f; started_ = now; pending_ = true; rx_ = tx_ = false; return true;
  }
  bool onReceive(const CanFrame& f) {
    if (halted_ || !pending_ || rx_ || !validEnvelope(f) || f.remote ||
        f.id != expected_.id || f.extended != expected_.extended || f.dlc != expected_.dlc ||
        memcmp(f.data, expected_.data, f.dlc) != 0) return false;
    rx_ = true; return true;
  }
  bool onTxSuccess() { if (halted_ || !pending_ || tx_) return false; tx_ = true; return true; }
  bool finish(uint32_t driverPending) {
    if (halted_ || !pending_ || !rx_ || !tx_ || driverPending != 0) return false;
    pending_ = false; return true;
  }
  bool timedOut(uint32_t now, uint32_t timeout) const { return !halted_ && pending_ && uint32_t(now-started_) >= timeout; }
  bool halt() { if (halted_) return false; halted_ = true; return true; }
  bool pending() const { return pending_; }
  bool halted() const { return halted_; }
  bool txSeen() const { return tx_; }
 private:
  CanFrame expected_{};
  uint32_t started_ = 0;
  bool pending_ = false, rx_ = false, tx_ = false, halted_ = false;
};
