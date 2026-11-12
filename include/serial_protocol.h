// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>

class SerialProtocol {
 public:
  static constexpr size_t MAX_CMD_LEN = 64;

  SerialProtocol() = default;

  // Feeds a single character from Serial. Returns true when a complete command is ready in cmdBuffer()
  bool feed(char ch);
  const char* command() const { return buffer_; }
  void reset();

 private:
  char buffer_[MAX_CMD_LEN]{};
  size_t length_ = 0;
  bool overflow_ = false;
};
