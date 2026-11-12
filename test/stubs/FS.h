#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef SeekSet
#define SeekSet 0
#endif
#ifndef SeekCur
#define SeekCur 1
#endif
#ifndef SeekEnd
#define SeekEnd 2
#endif

namespace fs {
class File {
 public:
  operator bool() const { return false; }
  size_t write(const uint8_t*, size_t) { return 0; }
  int read() { return -1; }
  size_t read(uint8_t*, size_t) { return 0; }
  int available() { return 0; }
  int readBytes(char*, size_t) { return 0; }
  bool seek(uint32_t, int = 0) { return true; }
  void flush() {}
  void close() {}
  size_t size() const { return 0; }
};

class FS {
 public:
  bool begin() { return true; }
  void end() {}
  File open(const char* = nullptr, const char* = "r") { return File(); }
  bool exists(const char* = nullptr) { return false; }
  bool remove(const char* = nullptr) { return false; }
  bool rename(const char* = nullptr, const char* = nullptr) { return true; }
  size_t totalBytes() { return 1000000; }
  size_t usedBytes() { return 0; }
};
}
using fs::FS;
using fs::File;
