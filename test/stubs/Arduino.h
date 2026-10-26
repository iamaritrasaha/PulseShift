// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string>
struct TestSerial {
  int capacity = 4096;
  std::string output;
  int availableForWrite() const { return capacity; }
  size_t write(const uint8_t* data, size_t size) { output.append(reinterpret_cast<const char*>(data),size); return size; }
};
inline TestSerial Serial;

inline uint32_t millis() { return 0; }
