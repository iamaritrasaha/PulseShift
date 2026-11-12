// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "can_recorder.h"
#include "diag_logger.h"
#include <Arduino.h>
#include <string.h>

uint16_t CanRecorder::computeCrc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc = crc << 1;
    }
  }
  return crc;
}

const char* CanRecorder::stageName(Stage s) const {
  switch (s) {
    case STAGE_KEY_ON_ENGINE_OFF: return "KEY_ON_ENGINE_OFF";
    case STAGE_ENGINE_IDLING:     return "ENGINE_IDLING";
    case STAGE_GENTLE_RPM:        return "GENTLE_RPM_SWEEP";
    default:                      return "STANDBY";
  }
}

void CanRecorder::begin(uint32_t sessionId, uint32_t bootCount, uint32_t bitrate, const char* modeName, const char* vehicle) {
  sessionId_ = sessionId;
  bootCount_ = bootCount;
  bitrate_ = bitrate;
  if (modeName) strncpy(modeName_, modeName, sizeof(modeName_) - 1);
  if (vehicle) strncpy(vehicle_, vehicle, sizeof(vehicle_) - 1);

  head_ = 0;
  tail_ = 0;
  count_ = 0;
  recordedFrames_ = 0;
  droppedFrames_ = 0;
  storageFull_ = false;
  recording_ = true;
  lastFlushMs_ = millis();

  auto& sm = StorageManager::instance();
  if (sm.isMounted()) {
    if (!sm.exists(CAPTURE_PATH)) {
      writeHeader();
    } else {
      // Existing file present: check if already full
      const size_t sz = sm.size(CAPTURE_PATH);
      if (sz >= MAX_CAPTURE_SIZE || sm.freeBytes() < StorageManager::MIN_FREE_BYTES) {
        storageFull_ = true;
      }
    }
  }

  initialized_ = true;
}

void CanRecorder::writeHeader() {
  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || sm.isStorageFull()) return;

  CaptureHeader hdr{};
  memcpy(hdr.magic, "PULSECAP", 8);
  hdr.headerVersion = 1;
  hdr.recordSize = sizeof(CanLogRecord);
  hdr.sessionId = sessionId_;
  hdr.bootCount = bootCount_;
  hdr.bitrate = bitrate_;
  hdr.startMs = millis();
  strncpy(hdr.mode, modeName_, sizeof(hdr.mode) - 1);
  strncpy(hdr.vehicle, vehicle_, sizeof(hdr.vehicle) - 1);

  File f = sm.open(CAPTURE_PATH, "w");
  if (f) {
    f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    f.flush();
    f.close();
  }
}

bool CanRecorder::push(const CanFrame& frame) {
  if (!initialized_ || !recording_ || storageFull_) return false;

  CanLogRecord rec{};
  rec.timestampMs = frame.timestampMs;
  rec.id = (frame.id & 0x1FFFFFFFUL) |
           (frame.extended ? 0x80000000UL : 0) |
           (frame.remote ? 0x40000000UL : 0);
  rec.dlc = (frame.dlc <= 8) ? frame.dlc : 8;
  rec.stage = static_cast<uint8_t>(currentStage_);
  if (!frame.remote && rec.dlc > 0) {
    memcpy(rec.data, frame.data, rec.dlc);
  }
  // Compute CRC16 over the first 18 bytes
  rec.checksum = computeCrc16(reinterpret_cast<const uint8_t*>(&rec), 18);

  if (count_ >= RING_CAPACITY) {
    ++droppedFrames_;
    return false;
  }

  ring_[head_] = rec;
  head_ = (head_ + 1) % RING_CAPACITY;
  ++count_;
  return true;
}

void CanRecorder::pump(uint32_t now) {
  if (!initialized_) return;

  // Flush conditions: batch size >= 16 records OR interval >= 500ms
  if (count_ >= 16 || (count_ > 0 && uint32_t(now - lastFlushMs_) >= 500)) {
    flush();
  }
}

void CanRecorder::flush() {
  lastFlushMs_ = millis();
  if (count_ == 0) return;

  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) {
    droppedFrames_ += count_;
    count_ = 0;
    head_ = tail_;
    return;
  }

  // Check storage space limit
  const size_t curSize = sm.size(CAPTURE_PATH);
  if (curSize >= MAX_CAPTURE_SIZE || sm.freeBytes() < StorageManager::MIN_FREE_BYTES) {
    if (!storageFull_) {
      storageFull_ = true;
      DiagLogger::instance().logEvent(DiagLevel::Error, "STORAGE_FULL",
                                     "Capture file reached limit (%lu bytes)", (unsigned long)curSize);
    }
    droppedFrames_ += count_;
    count_ = 0;
    head_ = tail_;
    return;
  }

  File f = sm.open(CAPTURE_PATH, "a");
  if (!f) {
    droppedFrames_ += count_;
    count_ = 0;
    head_ = tail_;
    return;
  }

  // Write in batches from ring buffer
  while (count_ > 0) {
    const auto& rec = ring_[tail_];
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(CanLogRecord));
    if (written == sizeof(CanLogRecord)) {
      ++recordedFrames_;
    } else {
      ++droppedFrames_;
      break;
    }
    tail_ = (tail_ + 1) % RING_CAPACITY;
    --count_;
  }

  f.flush();
  f.close();
}

void CanRecorder::setStage(Stage s) {
  if (currentStage_ == s) return;
  currentStage_ = s;
  DiagLogger::instance().logStageChange(static_cast<uint8_t>(s), stageName(s));
}

void CanRecorder::nextStage() {
  switch (currentStage_) {
    case STAGE_KEY_ON_ENGINE_OFF:
      setStage(STAGE_ENGINE_IDLING);
      break;
    case STAGE_ENGINE_IDLING:
      setStage(STAGE_GENTLE_RPM);
      break;
    case STAGE_GENTLE_RPM:
    default:
      setStage(STAGE_KEY_ON_ENGINE_OFF);
      break;
  }
}

size_t CanRecorder::currentFileSize() const {
  auto& sm = StorageManager::instance();
  return sm.size(CAPTURE_PATH);
}

void CanRecorder::dumpToSerial() {
  flush();
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) {
    Serial.println("[CAPTURE] Storage not mounted.");
    return;
  }

  if (!sm.exists(CAPTURE_PATH)) {
    Serial.println("[CAPTURE] No capture file found.");
    return;
  }

  File f = sm.open(CAPTURE_PATH, "r");
  if (!f) {
    Serial.println("[CAPTURE] Could not open capture file.");
    return;
  }

  const size_t totalBytes = f.size();
  if (totalBytes < sizeof(CaptureHeader)) {
    Serial.println("[CAPTURE] File truncated or invalid.");
    f.close();
    return;
  }

  CaptureHeader hdr{};
  if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr) ||
      memcmp(hdr.magic, "PULSECAP", 8) != 0) {
    Serial.println("[CAPTURE] Invalid header magic.");
    f.close();
    return;
  }

  Serial.println("=== BEGIN PULSESHIFT RAW CAN CAPTURE ===");
  Serial.printf("[HEADER] magic=PULSECAP ver=%u recSize=%u sess=%lu boot=%lu bitrate=%lu startMs=%lu mode=%s vehicle=%s\r\n",
                hdr.headerVersion, hdr.recordSize, (unsigned long)hdr.sessionId,
                (unsigned long)hdr.bootCount, (unsigned long)hdr.bitrate,
                (unsigned long)hdr.startMs, hdr.mode, hdr.vehicle);
  Serial.println("timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok");

  uint32_t validCount = 0;
  uint32_t corruptCount = 0;

  CanLogRecord rec{};
  while (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) == sizeof(rec)) {
    const uint16_t expected = computeCrc16(reinterpret_cast<const uint8_t*>(&rec), 18);
    const bool crcOk = (rec.checksum == expected);
    if (crcOk) ++validCount; else ++corruptCount;

    const bool ext = (rec.id & 0x80000000UL) != 0;
    const bool rtr = (rec.id & 0x40000000UL) != 0;
    const uint32_t arbId = rec.id & 0x1FFFFFFFUL;

    char hexData[17] = "";
    for (uint8_t i = 0; i < rec.dlc && i < 8; ++i) {
      char byteStr[3];
      snprintf(byteStr, sizeof(byteStr), "%02X", rec.data[i]);
      strcat(hexData, byteStr);
    }

    Serial.printf("%lu,%0*lX,%d,%d,%u,%s,%u,%d\r\n",
                  (unsigned long)rec.timestampMs,
                  ext ? 8 : 3,
                  (unsigned long)arbId,
                  ext ? 1 : 0,
                  rtr ? 1 : 0,
                  rec.dlc,
                  hexData,
                  rec.stage,
                  crcOk ? 1 : 0);
  }

  f.close();
  Serial.printf("=== END PULSESHIFT RAW CAN CAPTURE valid=%lu corrupt=%lu totalBytes=%lu ===\r\n",
                (unsigned long)validCount, (unsigned long)corruptCount, (unsigned long)totalBytes);
}

void CanRecorder::clear() {
  flush();
  auto& sm = StorageManager::instance();
  if (sm.isMounted() && sm.exists(CAPTURE_PATH)) {
    sm.remove(CAPTURE_PATH);
  }
  recordedFrames_ = 0;
  droppedFrames_ = 0;
  storageFull_ = false;
  writeHeader();
  DiagLogger::instance().logEvent(DiagLevel::Info, "CAPTURE_CLEAR", "Capture file cleared.");
}
