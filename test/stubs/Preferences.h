// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Host test stub for the ESP32 NVS Preferences library. Storage-backed calls
// are inert: reads return the requested default and writes report failure
// sizes, matching an unwritten flash on the host.
class Preferences {
 public:
  bool begin(const char* = nullptr, bool readOnly = false) { (void)readOnly; return true; }
  void end() {}
  bool clear() { return true; }
  uint32_t getUInt(const char*, uint32_t defaultValue) { return defaultValue; }
  size_t putUInt(const char*, uint32_t) { return sizeof(uint32_t); }
  size_t getBytes(const char*, void*, size_t) { return 0; }
  size_t putBytes(const char*, const void*, size_t length) { (void)length; return 0; }
};
