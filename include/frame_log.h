// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "rpm_decoder.h"
// Bounded UART capture queue. Loss is counted, never silently sampled away.
class FrameLog {
 public:
  void push(const CanFrame&, DecodeResult);
  void pump();
  void status(const char* format, ...); // Bounded priority record; never waits for UART.
  uint32_t dropped() const { return dropped_; }
 private:
  struct Line { char text[112]; uint8_t size = 0; } lines_[64];
  uint8_t head_ = 0, tail_ = 0, count_ = 0;
  uint32_t dropped_ = 0;
};
