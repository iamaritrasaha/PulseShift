// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Host test stub for esp_partition: no partition is ever found, so the
// blank-partition scan conservatively reports "not blank".
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 0x80

struct esp_partition_t {
  size_t size = 0;
};

inline const esp_partition_t* esp_partition_find_first(uint32_t, uint32_t, const char*) {
  return nullptr;
}

inline int esp_partition_read(const esp_partition_t*, size_t, void*, size_t) {
  return ESP_FAIL;
}
