// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "diag_logger.h"
#include "ble_manager.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {
const char* resetReasonToName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "EXCEPTION_PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

const char* levelToName(DiagLevel lvl) {
  switch (lvl) {
    case DiagLevel::Info:     return "INFO";
    case DiagLevel::Warn:     return "WARN";
    case DiagLevel::Error:    return "ERROR";
    case DiagLevel::Critical: return "CRITICAL";
  }
  return "DEBUG";
}
}  // namespace

void DiagLogger::begin(uint32_t bootCount, uint32_t sessionId) {
  bootCount_ = bootCount;
  sessionId_ = sessionId;
  errorCount_ = 0;
  warnCount_ = 0;
  logFull_ = false;
  bufferPos_ = 0;
  lastFlushMs_ = millis();

  esp_reset_reason_t r = esp_reset_reason();
  strncpy(resetReasonStr_, resetReasonToName(r), sizeof(resetReasonStr_) - 1);
  resetReasonStr_[sizeof(resetReasonStr_) - 1] = '\0';
}

void DiagLogger::logBoot(const char* modeName) {
  logEvent(DiagLevel::Info, "BOOT",
           "reason=%s boot=%lu sess=%lu mode=%s sdk=%s uptime=%lu",
           resetReasonStr_, (unsigned long)bootCount_, (unsigned long)sessionId_,
           modeName, ESP.getSdkVersion(), (unsigned long)millis());
  flush(); // Ensure boot header is flushed immediately to flash
}

void DiagLogger::logEvent(DiagLevel level, const char* tag, const char* format, ...) {
  if (level == DiagLevel::Warn) ++warnCount_;
  else if (level == DiagLevel::Error || level == DiagLevel::Critical) ++errorCount_;

  char msg[256];
  va_list args;
  va_start(args, format);
  vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  // Keep the most recent failure for status reporting (BLE diagnostics blob).
  if (level == DiagLevel::Error || level == DiagLevel::Critical) {
    snprintf(lastError_, sizeof(lastError_), "%s: %s", tag, msg);
  }

  char line[320];
  const int n = snprintf(line, sizeof(line), "[t=%lu,boot=%lu,sess=%lu] %s %s: %s\r\n",
                         (unsigned long)millis(), (unsigned long)bootCount_,
                         (unsigned long)sessionId_, levelToName(level), tag, msg);
  if (n > 0) {
    writeBuffered(line, size_t(n));
    BleManager::instance().queueLiveLog(line);
  }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.printf("DIAG,%lu,%s,%s,%s\r\n", (unsigned long)millis(), levelToName(level), tag, msg);
#endif

  // If critical/error, flush immediately
  if (level == DiagLevel::Critical || level == DiagLevel::Error) {
    flush();
  }
}

void DiagLogger::logCanInit(uint32_t bitrate, int errCode, const char* errName) {
  DiagLevel lvl = (errCode == 0) ? DiagLevel::Info : DiagLevel::Error;
  logEvent(lvl, "CAN_INIT", "bitrate=%lu result=%d (%s)",
           (unsigned long)bitrate, errCode, errName ? errName : "UNKNOWN");
  flush();
}

void DiagLogger::logCanHalted(const char* reason) {
  logEvent(DiagLevel::Critical, "CAN_HALTED", "reason=%s", reason ? reason : "unspecified");
  flush();
}

void DiagLogger::logCanInactivity(uint32_t silentMs) {
  logEvent(DiagLevel::Warn, "CAN_INACTIVE", "no frames for %lu ms", (unsigned long)silentMs);
}

void DiagLogger::logCanResume(uint32_t totalRx) {
  logEvent(DiagLevel::Info, "CAN_RESUME", "frames received after silence; totalRx=%lu", (unsigned long)totalRx);
}

void DiagLogger::logStageChange(uint8_t stage, const char* stageName) {
  logEvent(DiagLevel::Info, "STAGE_CHANGE", "stage=%u (%s)", stage, stageName ? stageName : "");
  flush(); // Stage changes are high priority
}

void DiagLogger::logStorageState(bool mounted, size_t used, size_t freeBytes, bool full) {
  DiagLevel lvl = (!mounted || full) ? DiagLevel::Error : DiagLevel::Info;
  logEvent(lvl, "STORAGE", "mounted=%d used=%lu free=%lu full=%d",
           mounted ? 1 : 0, (unsigned long)used, (unsigned long)freeBytes, full ? 1 : 0);
  flush();
}

void DiagLogger::writeBuffered(const char* str, size_t len) {
  if (len == 0) return;

  // If buffer doesn't fit, flush first
  if (bufferPos_ + len >= BUFFER_SIZE) {
    flush();
  }

  // If single string exceeds buffer size, write directly
  if (len >= BUFFER_SIZE) {
    if (StorageManager::instance().isMounted() && !StorageManager::instance().isStorageFull()) {
      checkRotate();
      File f = StorageManager::instance().open(LOG_PATH, "a");
      if (f) {
        f.write(reinterpret_cast<const uint8_t*>(str), len);
        f.flush();
        f.close();
      }
    }
    return;
  }

  memcpy(writeBuffer_ + bufferPos_, str, len);
  bufferPos_ += len;

  // Periodic flush every 2 seconds
  const uint32_t now = millis();
  if (uint32_t(now - lastFlushMs_) >= 2000) {
    flush();
  }
}

void DiagLogger::pump(uint32_t now) {
  if (bufferPos_ > 0 && uint32_t(now - lastFlushMs_) >= 2000) {
    flush();
  }
}

void DiagLogger::checkRotate() {
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) return;

  if (sm.exists(LOG_PATH)) {
    const size_t sz = sm.size(LOG_PATH);
    if (sz >= MAX_LOG_SIZE || sm.freeBytes() < StorageManager::MIN_FREE_BYTES) {
      logFull_ = true;
    }
  }
}

void DiagLogger::flush() {
  lastFlushMs_ = millis();
  if (bufferPos_ == 0) return;

  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || sm.isStorageFull() || logFull_) {
    bufferPos_ = 0;
    return;
  }

  checkRotate();
  if (logFull_) {
    bufferPos_ = 0;
    return;
  }

  File f = sm.open(LOG_PATH, "a");
  if (f) {
    f.write(reinterpret_cast<const uint8_t*>(writeBuffer_), bufferPos_);
    f.flush();
    f.close();
  }
  bufferPos_ = 0;
}

void DiagLogger::dumpToSerial() {
  flush();
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) {
    Serial.println("[LOGS] Storage is not mounted.");
    return;
  }

  if (!sm.exists(LOG_PATH)) {
    Serial.println("[LOGS] No diagnostic logs stored.");
    return;
  }

  File f = sm.open(LOG_PATH, "r");
  if (!f) {
    Serial.println("[LOGS] Error opening log file.");
    return;
  }

  Serial.println("=== BEGIN PULSESHIFT DIAGNOSTIC LOGS ===");
  while (f.available()) {
    char buf[128];
    const int n = f.readBytes(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      Serial.print(buf);
    }
  }
  f.close();
  Serial.println("=== END PULSESHIFT DIAGNOSTIC LOGS ===");
}

void DiagLogger::clear() {
  bufferPos_ = 0;
  logFull_ = false;
  auto& sm = StorageManager::instance();
  if (sm.isMounted() && sm.exists(LOG_PATH)) {
    sm.remove(LOG_PATH);
  }
  logEvent(DiagLevel::Info, "LOG_CLEAR", "Diagnostic log cleared by user request.");
  flush();
}
