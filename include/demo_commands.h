// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Fixed storage; discard an entire overlong/binary command, never execute its
// prefix. The caller consumes data immediately after Ready, then feeds again.
class DemoCommandLine {
 public:
  enum class Result { None, Ready, Rejected };
  Result feed(char ch) {
    if (ch == '\r' || ch == '\n') {
      if (discard_) { reset(); return Result::Rejected; }
      if (size_ == 0) return Result::None;
      text_[size_] = '\0'; size_ = 0; return Result::Ready;
    }
    if (discard_) return Result::None;
    if ((ch < ' ' && ch != '\t') || static_cast<unsigned char>(ch) > 126 || size_ == sizeof(text_) - 1) {
      discard_ = true; return Result::None;
    }
    text_[size_++] = ch;
    return Result::None;
  }
  char* text() { return text_; }
 private:
  void reset() { size_ = 0; discard_ = false; text_[0] = '\0'; }
  char text_[64]{};
  size_t size_ = 0;
  bool discard_ = false;
};

// Decimal digits only (optional surrounding spaces/tabs). No signs, trailing
// junk, empty values, or overflow; failure preserves the output value.
inline bool parseDemoUnsigned(const char* text, uint32_t maximum, uint32_t& output) {
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
  output = value; return true;
}
