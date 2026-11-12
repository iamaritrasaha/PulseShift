// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "storage_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_partition.h>

bool StorageManager::isPartitionBlank() const {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!part) return false;

  // Inspect first 1024 bytes of the data partition
  uint8_t buf[256];
  for (size_t offset = 0; offset < 1024; offset += sizeof(buf)) {
    if (esp_partition_read(part, offset, buf, sizeof(buf)) != ESP_OK) {
      return false;
    }
    for (size_t i = 0; i < sizeof(buf); i++) {
      if (buf[i] != 0xFF) {
        return false; // Found non-0xFF byte: partition has written data or existing filesystem
      }
    }
  }
  return true; // All 1024 bytes are 0xFF: virgin unformatted flash
}

bool StorageManager::begin(bool allowFormatIfCorrupt) {
  // Step 1: Attempt non-destructive mount without formatting
  mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");

  if (!mounted_) {
    // Check if the partition is virgin unformatted flash (all 0xFF).
    // On a brand-new ESP32 out of the box, the partition is unformatted.
    // Formatting virgin flash on first boot is essential so persistent storage
    // works without manual USB intervention, while NEVER wiping an existing partition with data.
    const bool isVirgin = isPartitionBlank();

    if (isVirgin || allowFormatIfCorrupt) {
      Serial.println(isVirgin 
        ? "[STORAGE] Virgin unformatted partition detected. Formatting LittleFS on first boot..."
        : "[STORAGE] Formatting LittleFS as explicitly requested...");
      if (LittleFS.format()) {
        mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
      }
    } else {
      // Partition is NOT blank (contains existing data or previous recordings).
      // Automatic formatting is strictly suppressed to prevent accidental data loss.
      Serial.println("[STORAGE WARNING] LittleFS mount failed on partition containing data. "
                     "Automatic format suppressed to protect existing recordings. "
                     "Use serial command 'CLEAR YES' via USB if you wish to wipe.");
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
