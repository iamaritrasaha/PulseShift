// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "can_frame.h"
#include "rpm_decoder.h"

// Demonstration protocol only: ID 555, exact DLC 8, RPM LE16, sequence LE32,
// bytes 6..7 = A5 5A. Neither the ID nor the encoding describes the NS125.
RpmMapping demonstrationMapping();
CanFrame demonstrationFrame(uint32_t now, uint32_t sequence);

class ReplayTransport {
 public:
  bool receive(uint32_t now, CanFrame& frame);
  void setManualRpm(int32_t rpm) { manualRpm_ = rpm < 0 ? -1 : (rpm > 65535 ? 65535 : rpm); }
  void setPaused(bool paused) { paused_ = paused; }
  bool isPaused() const { return paused_; }
  int32_t manualRpm() const { return manualRpm_; }

 private:
  uint32_t last_ = 0, sequence_ = 0;
  int32_t manualRpm_ = -1;  // -1: automated cyclical pattern; >=0: manual synthetic RPM
  bool paused_ = false;
};
