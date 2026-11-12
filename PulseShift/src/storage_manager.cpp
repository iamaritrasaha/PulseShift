// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "storage_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_partition.h>

bool StorageManager::isPartitionBlank() const {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!part || part->size == 0) return false;

  // Verify the entire partition in bounded 256-byte chunks.
  // Never classify a partition as blank based on just the first 1 KB;
  // valid recordings or filesystem structures could exist further in the partition.
  uint8_t buf[256];
  for (size_t offset = 0; offset < part->size; offset += sizeof(buf)) {
    const size_t toRead = (part->size - offset < sizeof(buf)) ? (part->size - offset) : sizeof(buf);
    if (esp_partition_read(part, offset, buf, toRead) != ESP_OK) {
      // If flash read fails, fail-safe: assume not blank to prevent accidental wipe
      return false;
    }
    for (size_t i = 0; i < toRead; ++i) {
      if (buf[i] != 0xFF) {
        // Found non-0xFF byte: partition contains data or previous filesystem
        return false;
      }
    }
  }
  return true; // Every byte across the entire partition is verified 0xFF (virgin flash)
}

bool StorageManager::begin(bool allowFormatIfCorrupt) {
  // Step 1: Attempt non-destructive mount without formatting
  mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");

  if (!mounted_) {
    // Check if the partition is genuinely blank (every byte across the entire partition is 0xFF).
    // On a brand-new ESP32 out of the box, the partition is unformatted.
    // Formatting virgin flash on first boot ensures persistent storage works out-of-the-box,
    // while NEVER formatting an existing partition that contains data.
    const bool isVirgin = isPartitionBlank();

    if (isVirgin || allowFormatIfCorrupt) {
      Serial.println(isVirgin 
        ? "[STORAGE] Blank unformatted partition verified (all 0xFF). Formatting LittleFS for first boot..."
        : "[STORAGE] Formatting LittleFS as explicitly requested...");
      if (LittleFS.format()) {
        mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
        if (mounted_) {
          Serial.println("[STORAGE] LittleFS formatted and mounted successfully.");
        } else {
          Serial.println("[STORAGE ERROR] LittleFS format succeeded, but mount failed.");
        }
      } else {
        Serial.println("[STORAGE ERROR] LittleFS format failed.");
      }
    } else {
      // Partition is NOT blank (contains non-0xFF bytes, such as existing recordings or corrupted filesystem).
      // Automatic formatting is strictly suppressed to protect potentially recoverable data.
      Serial.println("[STORAGE WARNING] LittleFS mount failed on partition containing data.");
      Serial.println("[STORAGE WARNING] Automatic format suppressed to protect existing recordings.");
      Serial.println("[STORAGE WARNING] Use Serial Monitor command 'CLEAR YES' via USB if you wish to wipe.");
    }
  }

  mountFailed_ = !mounted_;

  if (mounted_) {
    storageFull_ = (freeBytes() < MIN_FREE_BYTES);
  } else {
    storageFull_ = false;
  }

  // Step 2: Initialize NVS preferences for monotonic boot & session tracking
  Preferences prefs;
  if (prefs.begin("ps_diag", false)) {
    bootCount_ = prefs.getUInt("boot_cnt", 0) + 1;
    prefs.putUInt("boot_cnt", bootCount_);
    sessionId_ = prefs.getUInt("sess_id", 0);
    prefs.end();
  } else {
    bootCount_ = 1;
    sessionId_ = 0;
  }

  return mounted_;
}

size_t StorageManager::totalBytes() {
  if (!mounted_) return 0;
  return LittleFS.totalBytes();
}

size_t StorageManager::usedBytes() {
  if (!mounted_) return 0;
  return LittleFS.usedBytes();
}

size_t StorageManager::freeBytes() {
  if (!mounted_) return 0;
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return (total > used) ? (total - used) : 0;
}

bool StorageManager::format() {
  if (!mounted_) {
    // Try to mount or format directly
    if (LittleFS.format()) {
      mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
      mountFailed_ = !mounted_;
      storageFull_ = false;
      return mounted_;
    }
    return false;
  }
  const bool ok = LittleFS.format();
  storageFull_ = false;
  return ok;
}

File StorageManager::open(const char* path, const char* mode) {
  if (!mounted_) return File();
  return LittleFS.open(path, mode);
}

bool StorageManager::exists(const char* path) {
  if (!mounted_) return false;
  return LittleFS.exists(path);
}

bool StorageManager::remove(const char* path) {
  if (!mounted_) return false;
  return LittleFS.remove(path);
}

size_t StorageManager::size(const char* path) {
  if (!mounted_ || !LittleFS.exists(path)) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const size_t s = f.size();
  f.close();
  return s;
}

uint32_t StorageManager::incrementBootCount() {
  Preferences prefs;
  if (prefs.begin("ps_diag", false)) {
    bootCount_ = prefs.getUInt("boot_cnt", 0) + 1;
    prefs.putUInt("boot_cnt", bootCount_);
    prefs.end();
  }
  return bootCount_;
}

uint32_t StorageManager::nextSessionId() {
  Preferences prefs;
  if (prefs.begin("ps_diag", false)) {
    sessionId_ = prefs.getUInt("sess_id", 0) + 1;
    prefs.putUInt("sess_id", sessionId_);
    prefs.end();
  } else {
    ++sessionId_;
  }
  return sessionId_;
}
