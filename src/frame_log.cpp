// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "frame_log.h"
#include "config.h"
#include <Arduino.h>
#include <stdarg.h>
void FrameLog::push(const CanFrame& f, DecodeResult result) {
#if REVIX_SERIAL_DIAGNOSTICS
  if (count_ == 64) { ++dropped_; return; }
  auto& line = lines_[head_];
  int n = snprintf(line.text, sizeof(line.text), "RX t=%lu id=%08lX ext=%d rtr=%d dlc=%u data=",
    (unsigned long)f.timestampMs, (unsigned long)f.id, f.extended, f.remote, f.dlc);
  if (n < 0) n = 0;
  for (unsigned i = 0; !f.remote && i < f.dlc && i < f.payloadSize && i < 8; ++i) {
    const int rem = int(sizeof(line.text)) - n;
    if (rem <= 0) break;
    const int w = snprintf(line.text+n, size_t(rem), "%02X", f.data[i]);
    if (w > 0) n += w;
  }
  {
    const int rem = int(sizeof(line.text)) - n;
    if (rem > 0) {
      const int w = snprintf(line.text+n, size_t(rem), " %s\r\n", decodeName(result));
      if (w > 0) n += w;
    }
  }
  line.size = uint8_t(n < int(sizeof(line.text)) ? n : int(sizeof(line.text)) - 1);
  head_ = (head_+1) % 64; ++count_;
#else
  // Diagnostics are disabled, so do not retain frames in the UART log queue.
  (void)f;
  (void)result;
#endif
}
void FrameLog::pump() {
#if REVIX_SERIAL_DIAGNOSTICS
  if (!count_) return;
  auto& line = lines_[tail_];
  const int free = Serial.availableForWrite();
  // Whole lines only: periodic diagnostics cannot split a captured frame.
  if (free < line.size) return;
  Serial.write(reinterpret_cast<const uint8_t*>(line.text), line.size);
  tail_ = (tail_+1) % 64; --count_;
#endif
}

void FrameLog::status(const char* format, ...) {
#if REVIX_SERIAL_DIAGNOSTICS
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
