// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "storage_manager.h"
#include <Arduino.h>
#include <Preferences.h>

bool StorageManager::begin(bool allowFormatIfCorrupt) {
  // Step 1: Attempt mount without formatting
  mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
  if (!mounted_ && allowFormatIfCorrupt) {
    // If explicitly permitted (e.g. after user confirmation), attempt format
    LittleFS.format();
    mounted_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
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
