// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "diag_logger.h"
#include "ble_manager.h"
#include <Arduino.h>
#if defined(ARDUINO)
#include <esp_task_wdt.h>
#endif
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
  // Before any new boot logs are written, preserve a full previous log.
  // Never overwrite an earlier archive or alter the CAN recording.
  checkRotate();

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
    // snprintf returns the *would-have-written* length on truncation.
    // Never read beyond line[] when a long diagnostic message is formatted.
    size_t written = static_cast<size_t>(n);
    if (written >= sizeof(line)) {
      written = sizeof(line) - 1;
      line[written - 2] = '\r';
      line[written - 1] = '\n';
      line[written] = '\0';
    }
    writeBuffered(line, written);
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
    auto& sm = StorageManager::instance();
    if (sm.isMounted() && !sm.isStorageFull() && !logFull_) {
      checkRotate(len);
      const size_t stored = sm.exists(LOG_PATH) ? sm.size(LOG_PATH) : 0;
      if (!logFull_ && diagnosticLogAppendFits(stored, len, MAX_LOG_SIZE)) {
        File f = sm.open(LOG_PATH, "a");
        if (f) {
          f.write(reinterpret_cast<const uint8_t*>(str), len);
          f.flush();
          f.close();
        }
      } else {
        logFull_ = true; // No partial write: preserve existing log.
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

void DiagLogger::checkRotate(size_t pendingBytes) {
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) return;

  // Do not write when the filesystem has lost its reserved safety headroom.
  // Subtraction avoids overflow if pendingBytes is untrusted.
  const size_t freeBytes = sm.freeBytes();
  if (freeBytes <= StorageManager::MIN_FREE_BYTES ||
      pendingBytes > freeBytes - StorageManager::MIN_FREE_BYTES) {
    logFull_ = true;
    return;
  }
  if (!sm.exists(LOG_PATH)) return;

  const size_t stored = sm.size(LOG_PATH);
  if (diagnosticLogAppendFits(stored, pendingBytes, MAX_LOG_SIZE)) return;

  // A single rename (not copy/delete) leaves the complete previous log
  // available for later export; an existing archive is NEVER replaced.
  if (!diagnosticLogCanArchive(stored, pendingBytes, MAX_LOG_SIZE,
                               sm.exists(ARCHIVE_LOG_PATH), freeBytes,
                               StorageManager::MIN_FREE_BYTES) ||
      !sm.rename(LOG_PATH, ARCHIVE_LOG_PATH)) {
    logFull_ = true;
    return;
  }
  logFull_ = false;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  Serial.println("[LOGS] Previous full diagnostic log archived safely. New log started.");
#endif
}

void DiagLogger::flush() {
  lastFlushMs_ = millis();
  if (bufferPos_ == 0) return;

  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || sm.isStorageFull() || logFull_) {
    bufferPos_ = 0;
    return;
  }

  checkRotate(bufferPos_);
  if (logFull_) {
    bufferPos_ = 0;
    return;
  }

  const size_t stored = sm.exists(LOG_PATH) ? sm.size(LOG_PATH) : 0;
  if (!diagnosticLogAppendFits(stored, bufferPos_, MAX_LOG_SIZE)) {
    // The existing file may already exceed the old limit. Keep it intact,
    // stop accepting new writes and never silently truncate diagnostic data.
    logFull_ = true;
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

  // Include both archive and current log, so no previously captured errors
  // become inaccessible after a rollover. This operation is read-only.
  const char* const paths[] = {ARCHIVE_LOG_PATH, LOG_PATH};
  const char* const labels[] = {"ARCHIVED", "CURRENT"};
  bool anyLogs = false;
  for (unsigned i = 0; i < 2; ++i) {
    if (!sm.exists(paths[i])) continue;
    File f = sm.open(paths[i], "r");
    if (!f) {
      Serial.printf("[LOGS] Cannot open %s diagnostic log.\r\n", labels[i]);
      continue;
    }
    anyLogs = true;
    Serial.printf("=== BEGIN PULSESHIFT %s DIAGNOSTIC LOGS ===\r\n", labels[i]);
    while (f.available()) {
      char buf[128];
      const int n = f.readBytes(buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        Serial.print(buf);
      }
#if defined(ARDUINO)
      // Export of a 128-KB log at 115200 baud can exceed the task WDT period.
      esp_task_wdt_reset();
#endif
    }
    f.close();
    Serial.printf("\r\n=== END PULSESHIFT %s DIAGNOSTIC LOGS ===\r\n", labels[i]);
  }
  if (!anyLogs) Serial.println("[LOGS] No diagnostic logs stored.");
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
