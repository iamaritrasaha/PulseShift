// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef ESP_OK
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
#endif

typedef uintptr_t esp_ota_handle_t;

enum esp_ota_img_states_t {
  ESP_OTA_IMG_NEW = 0,
  ESP_OTA_IMG_PENDING_VERIFY = 1,
  ESP_OTA_IMG_VALID = 2,
  ESP_OTA_IMG_INVALID = 3,
  ESP_OTA_IMG_ABORTED = 4,
  ESP_OTA_IMG_UNDEFINED = 5
};

#include "esp_partition.h"

inline const esp_partition_t* esp_ota_get_running_partition() {
  static esp_partition_t runningPart{0x140000, "app0"};
  return &runningPart;
}

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  static esp_partition_t updatePart{0x140000, "app1"};
  return &updatePart;
}

inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* out_handle) {
  if (out_handle) *out_handle = 1;
  return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) {
  return ESP_OK;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t) {
  return ESP_OK;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t) {
  return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  return ESP_OK;
}

inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
  return ESP_OK;
}

inline esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* out_state) {
  if (out_state) *out_state = ESP_OTA_IMG_VALID;
  return ESP_OK;
}

inline esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() {
  return ESP_OK;
}

inline bool esp_ota_check_rollback_is_possible() {
  return true;
}
