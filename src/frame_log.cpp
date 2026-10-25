// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "frame_log.h"
#include <Arduino.h>
void FrameLog::push(const CanFrame& f, DecodeResult result) {
  if (count_ == 64) { ++dropped_; return; }
  auto& line = lines_[head_];
  int n = snprintf(line.text, sizeof(line.text), "RX t=%lu id=%08lX ext=%d rtr=%d dlc=%u data=",
    (unsigned long)f.timestampMs, (unsigned long)f.id, f.extended, f.remote, f.dlc);
  for (unsigned i = 0; !f.remote && i < f.dlc && i < 8; ++i)
    n += snprintf(line.text+n, sizeof(line.text)-n, "%02X", f.data[i]);
  n += snprintf(line.text+n, sizeof(line.text)-n, " %s\r\n", decodeName(result));
  line.size = uint8_t(n);
  head_ = (head_+1) % 64; ++count_;
}
void FrameLog::pump() {
  if (!count_) return;
  auto& line = lines_[tail_];
  const int free = Serial.availableForWrite();
  // Whole lines only: periodic diagnostics cannot split a captured frame.
  if (free < line.size) return;
  Serial.write(reinterpret_cast<const uint8_t*>(line.text), line.size);
  tail_ = (tail_+1) % 64; --count_;
}
