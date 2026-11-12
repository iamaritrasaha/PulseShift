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

CanRecorder::State CanRecorder::state() const {
  if (!StorageManager::instance().isMounted()) return State::Unmounted;
  if (writeError_) return State::WriteFailure;
  if (flashPreserved_) return State::PreservedPriorSession;
  if (storageFull_) return State::StorageFull;
  if (!recording_) return State::Paused;
  return State::RecordingOk;
}

const char* CanRecorder::stateName(State s) const {
  switch (s) {
    case State::Unmounted:             return "UNMOUNTED";
    case State::WriteFailure:          return "WRITE_FAILURE";
    case State::StorageFull:           return "STORAGE_FULL";
    case State::PreservedPriorSession: return "FLASH_CAPTURE_PRESERVED";
    case State::Paused:                return "PAUSED";
    case State::RecordingOk:           return "RECORDING_OK";
    default:                           return "UNKNOWN";
  }
}

uint32_t CanRecorder::totalRecords() const {
  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || !sm.exists(CAPTURE_PATH)) return 0;
  const size_t sz = sm.size(CAPTURE_PATH);
  if (sz <= sizeof(CaptureHeader)) return 0;
  return static_cast<uint32_t>((sz - sizeof(CaptureHeader)) / sizeof(CanLogRecord));
}

uint32_t CanRecorder::totalPages() const {
  const uint32_t recs = totalRecords();
  if (recs == 0) return 1;
  return (recs + DUMP_PAGE_SIZE - 1) / DUMP_PAGE_SIZE;
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
  flashPreserved_ = false;
  writeError_ = false;
  dumpCursor_ = 0;
  recording_ = true;
  lastFlushMs_ = millis();

  auto& sm = StorageManager::instance();
  if (sm.isMounted()) {
    if (!sm.exists(CAPTURE_PATH)) {
      writeHeader();
    } else {
      const size_t sz = sm.size(CAPTURE_PATH);
      if (sz < sizeof(CaptureHeader)) {
        writeHeader();
      } else {
        CaptureHeader hdr{};
        File f = sm.open(CAPTURE_PATH, "r");
        bool validHeader = false;
        if (f) {
          if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr)) {
            if (memcmp(hdr.magic, "PULSECAP", 8) == 0 && hdr.headerVersion == 1) {
              validHeader = true;
            }
          }
          f.close();
        }

        if (validHeader && sz > sizeof(CaptureHeader)) {
          const size_t recordsInFile = (sz - sizeof(CaptureHeader)) / sizeof(CanLogRecord);
          // If boot count or session ID differs, this capture was recorded in an earlier boot/session
          if (hdr.bootCount != bootCount_ || hdr.sessionId != sessionId_) {
            flashPreserved_ = true;
            storageFull_ = true;
            recording_ = false;
            sessionId_ = hdr.sessionId;
            DiagLogger::instance().logEvent(DiagLevel::Warn, "FLASH_CAPTURE_PRESERVED",
                                           "Prior session %lu capture preserved (%lu records, %lu bytes). Use DUMP to export, or CLEAR YES to overwrite.",
                                           (unsigned long)hdr.sessionId, (unsigned long)recordsInFile, (unsigned long)sz);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
            Serial.printf("FLASH CAPTURE PRESERVED: session %lu has %lu records (%lu bytes). Use DUMP to export, or CLEAR YES to overwrite.\r\n",
                          (unsigned long)hdr.sessionId, (unsigned long)recordsInFile, (unsigned long)sz);
#endif
          } else {
            // Same session/boot: recover any trailing partial record at EOF
            const size_t dataLen = sz - sizeof(CaptureHeader);
            const size_t remainder = dataLen % sizeof(CanLogRecord);
            if (remainder != 0) {
              truncateToSize(CAPTURE_PATH, sz - remainder);
              DiagLogger::instance().logEvent(DiagLevel::Warn, "CAPTURE_RECOVERED",
                                             "Truncated %u partial bytes at EOF", (unsigned)remainder);
            }
            if (sm.size(CAPTURE_PATH) >= MAX_CAPTURE_SIZE || sm.freeBytes() < StorageManager::MIN_FREE_BYTES) {
              storageFull_ = true;
            }
          }
        }
      }
    }
  }

  initialized_ = true;
}

bool CanRecorder::truncateToSize(const char* path, size_t newSize) {
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) return false;
  File orig = sm.open(path, "r");
  if (!orig) return false;
  if (orig.size() <= newSize) {
    orig.close();
    return true;
  }

  // Safety check: verify sufficient free flash to write replacement copy
  if (sm.freeBytes() < newSize + StorageManager::MIN_FREE_BYTES) {
    orig.close();
    DiagLogger::instance().logEvent(DiagLevel::Warn, "TRUNCATE_SKIPPED",
                                   "Insufficient free flash (%lu bytes) to truncate; original capture preserved.",
                                   (unsigned long)sm.freeBytes());
    return false;
  }

  const char* tempPath = "/temp_cap.bin";
  File temp = sm.open(tempPath, "w");
  if (!temp) {
    orig.close();
    return false;
  }

  uint8_t buf[128];
  size_t copied = 0;
  bool writeError = false;
  while (orig.available() && copied < newSize) {
    size_t toRead = newSize - copied;
    if (toRead > sizeof(buf)) toRead = sizeof(buf);
    const size_t n = orig.read(buf, toRead);
    if (n == 0) break;
    const size_t written = temp.write(buf, n);
    if (written != n) {
      writeError = true;
      break;
    }
    copied += written;
  }
  orig.close();
  temp.flush();
  temp.close();

  // Validate replacement before touching the original file
  bool validReplacement = (!writeError && copied == newSize && sm.size(tempPath) == newSize);
  if (validReplacement && newSize >= 8) {
    File verifyFile = sm.open(tempPath, "r");
    if (verifyFile) {
      char magic[8];
      if (verifyFile.read(reinterpret_cast<uint8_t*>(magic), 8) != 8 || memcmp(magic, "PULSECAP", 8) != 0) {
        validReplacement = false;
      }
      verifyFile.close();
    } else {
      validReplacement = false;
    }
  }

  if (!validReplacement) {
    sm.remove(tempPath);
    DiagLogger::instance().logEvent(DiagLevel::Error, "TRUNCATE_FAILED",
                                   "Replacement verification failed; original capture preserved.");
    return false;
  }

  // Safely replace original only after validated replacement is ready
  sm.remove(path);
  sm.rename(tempPath, path);
  return true;
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

  // Flush when batch reaches 8 records (160 bytes) OR time elapsed >= 200ms
  if (count_ >= 8 || (count_ > 0 && uint32_t(now - lastFlushMs_) >= 200)) {
    flush();
  }
}

void CanRecorder::flush() {
  lastFlushMs_ = millis();
  if (count_ == 0) return;

  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || flashPreserved_) {
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
    writeError_ = true;
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
      writeError_ = true;
      storageFull_ = true;
      DiagLogger::instance().logEvent(DiagLevel::Error, "STORAGE_ERROR",
                                     "Write error while flushing record to flash.");
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
  // Commit all pending in-memory records under the previous stage before switching
  flush();
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

void CanRecorder::dumpPage(uint32_t pageIndex) {
  flush();
  if (recording_) {
    recording_ = false;
    Serial.println("[CAPTURE] Recording paused for safe export. Run 'CAPTURE RESUME' when done.");
  }
  auto& sm = StorageManager::instance();
  if (!sm.isMounted()) {
    Serial.println("[CAPTURE ERROR] Storage not mounted.");
    return;
  }

  if (!sm.exists(CAPTURE_PATH)) {
    Serial.println("[CAPTURE] No capture file found.");
    return;
  }

  File f = sm.open(CAPTURE_PATH, "r");
  if (!f) {
    Serial.println("[CAPTURE ERROR] Could not open capture file.");
    return;
  }

  const size_t totalBytes = f.size();
  if (totalBytes < sizeof(CaptureHeader)) {
    Serial.println("[CAPTURE ERROR] File truncated or invalid.");
    f.close();
    return;
  }

  CaptureHeader hdr{};
  if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr) ||
      memcmp(hdr.magic, "PULSECAP", 8) != 0) {
    Serial.println("[CAPTURE ERROR] Invalid header magic.");
    f.close();
    return;
  }

  const uint32_t recs = (totalBytes - sizeof(CaptureHeader)) / sizeof(CanLogRecord);
  if (recs == 0) {
    Serial.println("[CAPTURE] No CAN records stored in file.");
    f.close();
    return;
  }

  const uint32_t pages = (recs + DUMP_PAGE_SIZE - 1) / DUMP_PAGE_SIZE;
  if (pageIndex < 1) pageIndex = 1;
  if (pageIndex > pages) pageIndex = pages;

  const uint32_t startRec = (pageIndex - 1) * DUMP_PAGE_SIZE;
  const uint32_t recsInThisPage = (startRec + DUMP_PAGE_SIZE <= recs) ? DUMP_PAGE_SIZE : (recs - startRec);
  const uint32_t endRec = startRec + recsInThisPage;

  // Print BEGIN page marker
  if (pageIndex == 1) {
    Serial.printf("=== BEGIN PULSESHIFT RAW CAN CAPTURE [PAGE %lu/%lu: RECORDS %lu-%lu OF %lu] ===\r\n",
                  (unsigned long)pageIndex, (unsigned long)pages,
                  (unsigned long)(startRec + 1), (unsigned long)endRec, (unsigned long)recs);
    Serial.printf("[HEADER] magic=PULSECAP ver=%u recSize=%u sess=%lu boot=%lu bitrate=%lu startMs=%lu mode=%s vehicle=%s\r\n",
                  hdr.headerVersion, hdr.recordSize, (unsigned long)hdr.sessionId,
                  (unsigned long)hdr.bootCount, (unsigned long)hdr.bitrate,
                  (unsigned long)hdr.startMs, hdr.mode, hdr.vehicle);
    Serial.println("timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok");
  } else {
    Serial.printf("=== BEGIN PULSESHIFT PAGE %lu/%lu: RECORDS %lu-%lu OF %lu ===\r\n",
                  (unsigned long)pageIndex, (unsigned long)pages,
                  (unsigned long)(startRec + 1), (unsigned long)endRec, (unsigned long)recs);
  }

  // Seek to start of page
  const size_t offset = sizeof(CaptureHeader) + (startRec * sizeof(CanLogRecord));
  f.seek(offset, SeekSet);

  uint32_t validCount = 0;
  uint32_t corruptCount = 0;
  CanLogRecord rec{};
  for (uint32_t i = 0; i < recsInThisPage; ++i) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) {
      Serial.printf("[DUMP ERROR] Incomplete record at record index %lu\r\n", (unsigned long)(startRec + i + 1));
      break;
    }
    const uint16_t expected = computeCrc16(reinterpret_cast<const uint8_t*>(&rec), 18);
    const bool crcOk = (rec.checksum == expected);
    if (crcOk) ++validCount; else ++corruptCount;

    const bool ext = (rec.id & 0x80000000UL) != 0;
    const bool rtr = (rec.id & 0x40000000UL) != 0;
    const uint32_t arbId = rec.id & 0x1FFFFFFFUL;

    char hexData[17] = "";
    for (uint8_t b = 0; b < rec.dlc && b < 8; ++b) {
      char byteStr[3];
      snprintf(byteStr, sizeof(byteStr), "%02X", rec.data[b]);
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

  // Print END marker
  if (pageIndex == pages) {
    Serial.printf("=== END PULSESHIFT RAW CAN CAPTURE valid=%lu corrupt=%lu totalBytes=%lu ===\r\n",
                  (unsigned long)(startRec + validCount), (unsigned long)corruptCount, (unsigned long)totalBytes);
    dumpCursor_ = 0; // Reset cursor upon full completion
  } else {
    dumpCursor_ = endRec;
    Serial.printf("=== END PULSESHIFT PAGE %lu/%lu (RECORDS %lu-%lu OF %lu) [Next: 'DUMP NEXT' or 'DUMP ALL'] ===\r\n",
                  (unsigned long)pageIndex, (unsigned long)pages,
                  (unsigned long)(startRec + 1), (unsigned long)endRec, (unsigned long)recs);
  }
}

void CanRecorder::dumpNext() {
  const uint32_t recs = totalRecords();
  if (recs == 0) {
    dumpPage(1);
    return;
  }
  const uint32_t currentPage = (dumpCursor_ / DUMP_PAGE_SIZE) + 1;
  dumpPage(currentPage);
}

void CanRecorder::dumpAll() {
  flush();
  if (recording_) {
    recording_ = false;
    Serial.println("[CAPTURE] Recording paused for safe export. Run 'CAPTURE RESUME' when done.");
  }
  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || !sm.exists(CAPTURE_PATH)) {
    Serial.println("[CAPTURE] No capture file found.");
    return;
  }
  File f = sm.open(CAPTURE_PATH, "r");
  if (!f) {
    Serial.println("[CAPTURE ERROR] Could not open capture file.");
    return;
  }
  const size_t totalBytes = f.size();
  if (totalBytes < sizeof(CaptureHeader)) {
    Serial.println("[CAPTURE ERROR] File truncated or invalid.");
    f.close();
    return;
  }
  CaptureHeader hdr{};
  if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr) ||
      memcmp(hdr.magic, "PULSECAP", 8) != 0) {
    Serial.println("[CAPTURE ERROR] Invalid header magic.");
    f.close();
    return;
  }

  const uint32_t recs = (totalBytes - sizeof(CaptureHeader)) / sizeof(CanLogRecord);
  Serial.printf("=== BEGIN PULSESHIFT RAW CAN CAPTURE [ALL: %lu RECORDS] ===\r\n", (unsigned long)recs);
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
    for (uint8_t b = 0; b < rec.dlc && b < 8; ++b) {
      char byteStr[3];
      snprintf(byteStr, sizeof(byteStr), "%02X", rec.data[b]);
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
  dumpCursor_ = 0;
  Serial.printf("=== END PULSESHIFT RAW CAN CAPTURE valid=%lu corrupt=%lu totalBytes=%lu ===\r\n",
                (unsigned long)validCount, (unsigned long)corruptCount, (unsigned long)totalBytes);
}

void CanRecorder::dumpStatus() {
  const uint32_t recs = totalRecords();
  const uint32_t pages = totalPages();
  const uint32_t currentPg = (dumpCursor_ / DUMP_PAGE_SIZE) + 1;
  const uint32_t remaining = (dumpCursor_ < recs) ? (recs - dumpCursor_) : 0;

  Serial.println("=== PULSESHIFT DUMP STATUS ===");
  Serial.printf("Total Stored Records: %lu (%lu bytes)\r\n", (unsigned long)recs, (unsigned long)currentFileSize());
  Serial.printf("Total Pages: %lu (%lu records per page)\r\n", (unsigned long)pages, (unsigned long)DUMP_PAGE_SIZE);
  Serial.printf("Export Cursor: Record %lu (Page %lu of %lu)\r\n",
                (unsigned long)dumpCursor_, (unsigned long)currentPg, (unsigned long)pages);
  Serial.printf("Remaining to Export: %lu records\r\n", (unsigned long)remaining);
  Serial.println("Commands:");
  Serial.println("  DUMP        : Export next page (records 500 at a time)");
  Serial.println("  DUMP NEXT   : Export next page");
  Serial.println("  DUMP ALL    : Stream all records continuously without stopping");
  Serial.println("  DUMP PAGE n : Export specific page number n");
  Serial.println("  DUMP CANCEL : Reset export cursor to page 1");
  Serial.println("==============================");
}

void CanRecorder::dumpCancel() {
  dumpCursor_ = 0;
  Serial.println("[DUMP] Export cursor reset to beginning (Page 1).");
}

void CanRecorder::dumpPageNumber(uint32_t pageNumber) {
  const uint32_t pages = totalPages();
  if (pageNumber < 1 || pageNumber > pages) {
    Serial.printf("[DUMP ERROR] Invalid page number %lu. Valid pages: 1..%lu\r\n",
                  (unsigned long)pageNumber, (unsigned long)pages);
    return;
  }
  dumpPage(pageNumber);
}

void CanRecorder::clear() {
  flush();
  auto& sm = StorageManager::instance();
  if (sm.isMounted() && sm.exists(CAPTURE_PATH)) {
    sm.remove(CAPTURE_PATH);
  }
  sm.nextSessionId();
  sessionId_ = sm.currentSessionId();
  bootCount_ = sm.bootCount();
  flashPreserved_ = false;
  recordedFrames_ = 0;
  droppedFrames_ = 0;
  storageFull_ = false;
  writeError_ = false;
  recording_ = true;
  dumpCursor_ = 0;
  writeHeader();
  DiagLogger::instance().logEvent(DiagLevel::Info, "CAPTURE_CLEAR", "Capture cleared; fresh session started.");
}
