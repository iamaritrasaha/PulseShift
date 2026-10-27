// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
struct TestSerial {
  int capacity = 4096;
  std::string output;
  void println(const char* text) { output.append(text); output.append("\r\n"); }
  void printf(const char* format, ...) {
    char buffer[1024];va_list args;va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);output.append(buffer);
  }
  int availableForWrite() const { return capacity; }
  size_t write(const uint8_t* data, size_t size) { output.append(reinterpret_cast<const char*>(data),size); return size; }
};
inline TestSerial Serial;

inline uint32_t millis() { return 0; }
