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

  void begin(uint32_t bootCount, uint32_t sessionId);
  void logBoot(const char* modeName, bool rpmMapped);
  void logEvent(DiagLevel level, const char* tag, const char* format, ...);
  void logCanAlert(uint32_t alerts, uint32_t tec, uint32_t rec, uint32_t busErrors, uint32_t missed, uint32_t overrun);
  void logCanInit(uint32_t bitrate, int errCode, const char* errName);
  void logCanHalted(const char* reason);
  void logCanInactivity(uint32_t silentMs);
  void logCanResume(uint32_t totalRx);
  void logStageChange(uint8_t stage, const char* stageName);
  void logStorageState(bool mounted, size_t used, size_t freeBytes, bool full);

  void flush();
  void dumpToSerial();
  void clear();

  uint32_t errorCount() const { return errorCount_; }
  uint32_t warnCount() const { return warnCount_; }
  const char* resetReasonString() const { return resetReasonStr_; }

 private:
  DiagLogger() = default;
  ~DiagLogger() = default;

  void writeBuffered(const char* str, size_t len);
  void checkRotate();

  uint32_t bootCount_ = 0;
  uint32_t sessionId_ = 0;
  uint32_t errorCount_ = 0;
  uint32_t warnCount_ = 0;
  char resetReasonStr_[32]{"UNKNOWN"};

  char writeBuffer_[BUFFER_SIZE]{};
  size_t bufferPos_ = 0;
  uint32_t lastFlushMs_ = 0;
};
