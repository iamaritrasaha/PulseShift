#include "config_manager.h"
#include <nvs.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
  std::fprintf(stderr, "NVS FAIL %d: %s\n", __LINE__, #x); std::exit(1); \
} } while (0)

static std::map<std::string, std::vector<uint8_t>> flash;
static std::vector<uint8_t> uncommitted;
static bool opened = false;
static bool failCommit = false;
static bool failReadback = false;
static bool failOpen = false;

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* handle) {
  CHECK(std::strcmp(ns, "ps_cfg") == 0 && !opened);
  if (failOpen) return ESP_FAIL;
  *handle = mode;
  opened = true;
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* data, size_t* size) {
  CHECK(opened && handle == NVS_READONLY);
  if (failReadback) return ESP_FAIL;
  auto record = flash.find(key);
  if (record == flash.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (data && *size < record->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
  *size = record->second.size();
  if (data) std::memcpy(data, record->second.data(), *size);
  return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t size) {
  CHECK(opened && handle == NVS_READWRITE && std::strcmp(key, "blob4") == 0);
  const auto* bytes = static_cast<const uint8_t*>(data);
  uncommitted.assign(bytes, bytes + size);
  return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  CHECK(opened && handle == NVS_READWRITE);
  if (failCommit) return ESP_FAIL;
  flash["blob4"] = uncommitted;
  return ESP_OK;
}

void nvs_close(nvs_handle_t) {
  CHECK(opened);
  opened = false;
  uncommitted.clear();
}

static void putCrc(std::vector<uint8_t>& bytes) {
  const auto crc = ConfigManager::computeCrc32(bytes.data(), bytes.size() - sizeof(uint32_t));
  std::memcpy(bytes.data() + bytes.size() - sizeof(crc), &crc, sizeof(crc));
}

static std::vector<uint8_t> legacyRecord(const PulseShiftConfigV3& config) {
  std::vector<uint8_t> bytes(8 + sizeof(config) + 4);
  const uint32_t magic = 0x50534332;
  const uint16_t version = 3;
  const uint16_t length = bytes.size();
  std::memcpy(bytes.data(), &magic, sizeof(magic));
  std::memcpy(bytes.data() + 4, &version, sizeof(version));
  std::memcpy(bytes.data() + 6, &length, sizeof(length));
  std::memcpy(bytes.data() + 8, &config, sizeof(config));
  putCrc(bytes);
  return bytes;
}

int main() {
  auto& cfg = ConfigManager::instance();
  cfg.begin();
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::Missing);
  CHECK(cfg.runningLedCount() == 6);
  PulseShiftConfigV3 old{};
  old.effect = 4;
  old.calibrationVerified = true;
  old.calFingerprint = 0x12345678;
  old.calPointsUsed = 3;
  old.brightness = 108;
  old.activeRevision = 20;
  old.startupAnimation = 2;
  old.startupDurationMs = 0x8000 | 1500;
  const auto preserved = legacyRecord(old);
  flash["blob"] = preserved;
  cfg.begin();
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::Legacy);
  CHECK(cfg.active().calFingerprint == old.calFingerprint);
  CHECK(cfg.active().startupDurationMs == old.startupDurationMs);

  // Stage and commit the same kinds of fields that the Android preset writes.
  CHECK(cfg.beginTransaction());
  CHECK(!cfg.beginTransaction());
  CHECK(cfg.setBrightness(192, true) == BLE_CFG_STATUS_OK);
  CHECK(cfg.setStartupAnimation(4, 0x8000 | 3400, true) == BLE_CFG_STATUS_OK);
  CHECK(cfg.setEffect(3, 78, true) == BLE_CFG_STATUS_OK);
  CHECK(cfg.setLedRgb(5, {43, 211, 101}, true) == BLE_CFG_STATUS_OK);
  CHECK(cfg.setPhysicalLedCount(64, true) == BLE_CFG_STATUS_OK);
  CHECK(cfg.active().brightness == 108 && cfg.active().physicalLedCount == 6);
  CHECK(!cfg.saveToNvs() && flash.count("blob4") == 0);
  CHECK(cfg.commitTransaction() && cfg.saveToNvs());
  CHECK(cfg.activeMatchesNvs() && cfg.savedRevision() == 21 && cfg.runningLedCount() == 6);
  CHECK(flash["blob"] == preserved);

  cfg.begin();  // Recreate the boot read/decode path from committed storage.
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::Loaded);
  CHECK(cfg.activeMatchesNvs() && cfg.saveStatus() == ConfigSaveStatus::NoAttempt);
  CHECK(cfg.active().brightness == 192 && cfg.active().effect == 3 && cfg.active().effectSpeed == 78);
  CHECK((cfg.active().ledColors[5] == RgbColor{43, 211, 101}));
  CHECK(cfg.active().startupAnimation == 4 && cfg.active().startupDurationMs == (0x8000 | 3400));
  CHECK(cfg.runningLedCount() == 64);
  CHECK(cfg.active().calibrationVerified && cfg.active().calFingerprint == old.calFingerprint);

  CHECK(cfg.setBrightness(91, false) == BLE_CFG_STATUS_OK && !cfg.activeMatchesNvs());
  failOpen = true;
  CHECK(!cfg.saveToNvs() && cfg.saveStatus() == ConfigSaveStatus::OpenFailed);
  failOpen = false;
  failCommit = true;
  CHECK(!cfg.saveToNvs() && cfg.saveStatus() == ConfigSaveStatus::WriteFailed);
  failCommit = false;
  cfg.begin();
  CHECK(cfg.active().brightness == 192);
  failReadback = true;
  CHECK(!cfg.saveToNvs() && cfg.saveStatus() == ConfigSaveStatus::ReadbackFailed);
  CHECK(!cfg.activeMatchesNvs());
  failReadback = false;

  auto& blob = flash["blob4"];
  blob[blob.size() - 5] = 65;
  putCrc(blob);
  cfg.begin();
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::CountDefaulted);
  CHECK(cfg.runningLedCount() == 6 && cfg.active().brightness == 192);
  blob.back() ^= 0x01;
  cfg.begin();
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::RecoveredLegacy);
  CHECK(cfg.active().brightness == 108 && cfg.runningLedCount() == 6);
  CHECK(flash["blob"] == preserved);
  flash["blob"][6] ^= 1;   // Valid CRC over a malformed length must still fail.
  putCrc(flash["blob"]);
  cfg.begin();
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::Invalid);
  CHECK(cfg.runningLedCount() == 6 && cfg.active().brightness == 200 && !cfg.activeMatchesNvs());
  cfg.begin(false);
  CHECK(cfg.bootLoadStatus() == ConfigLoadStatus::Unavailable && !cfg.saveToNvs());
  std::puts("PASS boot migration, staged save, flash readback and corruption recovery");
}
