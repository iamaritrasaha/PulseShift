// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <FS.h>
#include <LittleFS.h>

class StorageManager {
 public:
  static constexpr size_t MIN_FREE_BYTES = 32768; // 32 KB safety headroom

  static StorageManager& instance() {
    static StorageManager mgr;
    return mgr;
  }

  bool begin(bool allowFormatIfCorrupt = false);
  bool isMounted() const { return mounted_; }
  bool mountFailed() const { return mountFailed_; }
  bool isStorageFull() const { return storageFull_; }
  void setStorageFull(bool full) { storageFull_ = full; }

  size_t totalBytes();
  size_t usedBytes();
  size_t freeBytes();

  bool format();
  File open(const char* path, const char* mode);
  bool exists(const char* path);
  bool remove(const char* path);
  size_t size(const char* path);

  // NVS metadata management
  uint32_t incrementBootCount();
  uint32_t bootCount() const { return bootCount_; }
  uint32_t nextSessionId();
  uint32_t currentSessionId() const { return sessionId_; }

 private:
  StorageManager() = default;
  ~StorageManager() = default;

  bool mounted_ = false;
  bool mountFailed_ = false;
  bool storageFull_ = false;
  uint32_t bootCount_ = 0;
  uint32_t sessionId_ = 0;
};
