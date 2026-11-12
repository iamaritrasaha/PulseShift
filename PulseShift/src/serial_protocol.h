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

// Parses decimal unsigned integer safely without signs or overflow
inline bool parseUnsignedNumber(const char* text, uint32_t maximum, uint32_t& output) {
  while (*text == ' ' || *text == '\t') ++text;
  if (*text < '0' || *text > '9') return false;
  uint32_t value = 0;
  do {
    const uint32_t digit = static_cast<uint32_t>(*text++ - '0');
    if (digit > maximum || value > (maximum - digit) / 10) return false;
    value = value * 10 + digit;
  } while (*text >= '0' && *text <= '9');
  while (*text == ' ' || *text == '\t') ++text;
  if (*text != '\0') return false;
  output = value;
  return true;
}

inline bool parseDemoUnsigned(const char* text, uint32_t maximum, uint32_t& output) {
  return parseUnsignedNumber(text, maximum, output);
}
