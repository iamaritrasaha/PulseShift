// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "serial_protocol.h"
#include <ctype.h>

bool SerialProtocol::feed(char ch) {
  if (ch == '\r' || ch == '\n') {
    if (overflow_) {
      reset();
      return false;
    }
    if (length_ == 0) return false;
    buffer_[length_] = '\0';
    return true; // Complete command ready
  }

  if (overflow_) return false;

  if (length_ >= MAX_CMD_LEN - 1) {
    overflow_ = true;
    return false;
  }

  // Accept printable characters
  if (isprint(static_cast<unsigned char>(ch))) {
    buffer_[length_++] = ch;
  }

  return false;
}

void SerialProtocol::reset() {
  length_ = 0;
  overflow_ = false;
  buffer_[0] = '\0';
}
