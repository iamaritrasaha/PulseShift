// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "can_frame.h"
#include "storage_manager.h"

// 20-byte packed CAN log record with CRC16
struct __attribute__((packed)) CanLogRecord {
  uint32_t timestampMs; // Relative ms from boot
  uint32_t id;          // bit 31: extended, bit 30: remote, bits 0-28: CAN arbitration ID
  uint8_t dlc;          // 0..8
  uint8_t stage;        // Capture stage: 1=KeyOn/Off, 2=Idle, 3=GentleRPM, 0=None
  uint8_t data[8];      // CAN payload bytes
  uint16_t checksum;    // CRC16 over preceding 18 bytes
};

// 76-byte header written at the start of a capture file
struct __attribute__((packed)) CaptureHeader {
  char magic[8];          // "PULSECAP"
  uint16_t headerVersion; // 1
  uint16_t recordSize;    // 20
  uint32_t sessionId;     // Monotonic session number
  uint32_t bootCount;     // Monotonic boot counter
  uint32_t bitrate;       // Configured bitrate (e.g. 500000)
  uint32_t startMs;       // Timestamp when recording began
  char mode[16];          // Mode name (e.g. "CAN_CAPTURE")
  char vehicle[32];       // Vehicle descriptor
};

class CanRecorder {
 public:
  static constexpr const char* CAPTURE_PATH = "/capture.bin";
  static constexpr size_t MAX_CAPTURE_SIZE = 1000000; // 1,000,000 bytes (~50,000 frames)
  static constexpr size_t RING_CAPACITY = 256;        // In-memory queue (5120 bytes)
  static constexpr size_t DUMP_PAGE_SIZE = 500;       // 500 records per export page

  enum Stage : uint8_t {
    STAGE_STANDBY = 0,
    STAGE_KEY_ON_ENGINE_OFF = 1,
    STAGE_ENGINE_IDLING = 2,
    STAGE_GENTLE_RPM = 3
  };

  enum class State : uint8_t {
    Unmounted,
    WriteFailure,
    DamagedRecording,
    StorageFull,
    PreservedPriorSession,
    Paused,
    RecordingOk
  };

  static CanRecorder& instance() {
    static CanRecorder rec;
    return rec;
  }

  void begin(uint32_t sessionId, uint32_t bootCount, uint32_t bitrate, const char* modeName, const char* vehicle);
  bool push(const CanFrame& frame);
  void pump(uint32_t now);
  void flush();

  void setStage(Stage s);
  Stage currentStage() const { return currentStage_; }
  const char* stageName(Stage s) const;
  void nextStage();

  void setRecording(bool enable) { recording_ = enable; }
  bool isRecording() const { return recording_; }
  bool isStorageFull() const { return storageFull_; }
  bool isFlashPreserved() const { return flashPreserved_; }
  bool isDamagedRecording() const { return damagedRecording_; }
  bool isWriteError() const { return writeError_; }

  State state() const;
  const char* stateName(State s) const;

  uint32_t recordedFrames() const { return recordedFrames_; }
  uint32_t droppedFrames() const { return droppedFrames_; }
  size_t currentFileSize() const;

  uint32_t totalRecords() const;
  uint32_t totalPages() const;
  uint32_t dumpCursor() const { return dumpCursor_; }
  uint32_t dumpCumValid() const { return dumpCumValid_; }
  uint32_t dumpCumCorrupt() const { return dumpCumCorrupt_; }
  uint32_t dumpExportedCount() const { return dumpExportedCount_; }

  void dumpPage(uint32_t pageIndex);
  void dumpNext();
  void dumpAll();
  void dumpStatus();
  void dumpCancel();
  void dumpPageNumber(uint32_t pageNumber);
  void dumpToSerial() { dumpNext(); }

  // ---- BLE capture export (bounded, resumable, non-destructive) ----
  // Pages of records streamed to the phone over the diagnostic-log
  // characteristic. Reading never mutates the capture file; the cursor is
  // independent of the serial DUMP cursor so both can interleave safely.
  static constexpr uint32_t BLE_EXPORT_PAGE_RECORDS = 12;
  bool bleExportBegin(uint32_t startRecord);             // false: no readable capture
  bool bleExportNext(CanLogRecord& out, uint32_t& recordIndex);  // false: end of capture
  uint32_t bleExportCursor() const { return bleCursor_; }
  void bleExportCancel() { bleActive_ = false; }
  bool isBleExportActive() const { return bleActive_; }
  bool bleExportChecksumOk() const { return bleChecksumOk_; }

  void clear();

  static uint16_t computeCrc16(const uint8_t* data, size_t length);

 private:
  CanRecorder() = default;
  ~CanRecorder() = default;

  void writeHeader();
  bool truncateToSize(const char* path, size_t newSize);
  static bool verifyCaptureFile(const char* filePath, size_t expectedSize, bool checkRecords);

  CanLogRecord ring_[RING_CAPACITY]{};
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;

  bool initialized_ = false;
  bool recording_ = true;
  bool storageFull_ = false;
  bool flashPreserved_ = false;
  bool damagedRecording_ = false;
  bool writeError_ = false;
  uint32_t dumpCursor_ = 0;
  uint32_t dumpCumValid_ = 0;
  uint32_t dumpCumCorrupt_ = 0;
  uint32_t dumpExportedCount_ = 0;
  Stage currentStage_ = STAGE_KEY_ON_ENGINE_OFF;

  uint32_t sessionId_ = 0;
  uint32_t bootCount_ = 0;
  uint32_t bitrate_ = 0;
  char modeName_[16]{"CAN_CAPTURE"};
  char vehicle_[32]{"NS125 ABS 2025"};

  uint32_t recordedFrames_ = 0;
  uint32_t droppedFrames_ = 0;
  uint32_t lastFlushMs_ = 0;

  // BLE export cursor state
  bool bleActive_ = false;
  bool bleChecksumOk_ = true;
  uint32_t bleCursor_ = 0;
};
