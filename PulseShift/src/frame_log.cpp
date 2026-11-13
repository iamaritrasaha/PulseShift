// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "frame_log.h"
#include "config.h"
#include <Arduino.h>
#include <stdarg.h>
void FrameLog::pump() {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  while (count_ > 0) {
    auto& line = lines_[tail_];
    const int free = Serial.availableForWrite();
    // Whole lines only: periodic diagnostics cannot split a captured frame.
    if (free < line.size) break;
    Serial.write(reinterpret_cast<const uint8_t*>(line.text), line.size);
    tail_ = (tail_+1) % 64; --count_;
  }
#endif
}

void FrameLog::status(const char* format, ...) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  char text[112];
  va_list args;
  va_start(args, format);
  const int n = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  // No partially formatted records. Count failures and evictions explicitly.
  if (n < 0 || n > int(sizeof(text))-3) { ++dropped_; return; }
  if (count_ == 64) { tail_ = (tail_+1)%64; --count_; ++dropped_; }
  auto& line = lines_[head_];
  memcpy(line.text, text, size_t(n));
  line.text[n] = '\r'; line.text[n+1] = '\n'; line.size = uint8_t(n+2);
  head_ = (head_+1)%64; ++count_;
#else
  (void)format;
#endif
}
