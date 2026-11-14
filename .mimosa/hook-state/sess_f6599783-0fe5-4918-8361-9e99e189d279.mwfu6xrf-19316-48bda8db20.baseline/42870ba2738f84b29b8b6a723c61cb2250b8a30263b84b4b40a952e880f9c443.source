// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "storage_manager.h"

enum class DiagLevel : uint8_t {
  Info = 0,
  Warn = 1,
  Error = 2,
  Critical = 3
};

class DiagLogger {
 public:
  static constexpr const char* LOG_PATH = "/diag.log";
  static constexpr size_t MAX_LOG_SIZE = 131072; // 128 KB max diagnostic log
  static constexpr size_t BUFFER_SIZE = 1024;    // 1 KB write buffer

  static DiagLogger& instance() {
    static DiagLogger logger;
    return logger;
  }

#if !defined(ARDUINO)
  void begin(uint32_t, uint32_t) {}
  void logBoot(const char*) {}
  void logEvent(DiagLevel, const char*, const char*, ...) {}
  void logCanInit(uint32_t, int, const char*) {}
  void logCanHalted(const char*) {}
  void logCanInactivity(uint32_t) {}
  void logCanResume(uint32_t) {}
  void logStageChange(uint8_t, const char*) {}
  void logStorageState(bool, size_t, size_t, bool) {}
  void pump(uint32_t) {}
  void flush() {}
  void dumpToSerial() {}
  void clear() {}
#else
  void begin(uint32_t bootCount, uint32_t sessionId);
  void logBoot(const char* modeName);
  void logEvent(DiagLevel level, const char* tag, const char* format, ...);
  void logCanInit(uint32_t bitrate, int errCode, const char* errName);
  void logCanHalted(const char* reason);
  void logCanInactivity(uint32_t silentMs);
  void logCanResume(uint32_t totalRx);
  void logStageChange(uint8_t stage, const char* stageName);
  void logStorageState(bool mounted, size_t used, size_t freeBytes, bool full);
  void pump(uint32_t now);
  void flush();
  void dumpToSerial();
  void clear();
#endif

  uint32_t errorCount() const { return errorCount_; }
  uint32_t warnCount() const { return warnCount_; }
  bool isLogFull() const { return logFull_; }
  const char* resetReasonString() const { return resetReasonStr_; }
  // Last Error/Critical message (without timestamp prefix), for diagnostics
  // reporting over BLE. Empty string when nothing has failed this session.
  const char* lastError() const { return lastError_; }

 private:
  DiagLogger() = default;
  ~DiagLogger() = default;

  void writeBuffered(const char* str, size_t len);
  void checkRotate();

  uint32_t bootCount_ = 0;
  uint32_t sessionId_ = 0;
  uint32_t errorCount_ = 0;
  uint32_t warnCount_ = 0;
  bool logFull_ = false;
  char resetReasonStr_[32]{"UNKNOWN"};
  char lastError_[96]{};

  char writeBuffer_[BUFFER_SIZE]{};
  size_t bufferPos_ = 0;
  uint32_t lastFlushMs_ = 0;
};
