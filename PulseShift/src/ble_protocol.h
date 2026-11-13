// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>

#pragma pack(push, 1)

// ============================================================================
// PulseShift BLE Protocol v1 Definitions
// ============================================================================

#define PULSESHIFT_BLE_PROTOCOL_VERSION 1

// Service UUID: fe12d210-3d4f-4b9d-a21c-92782ecb4000
#define PULSESHIFT_BLE_SERVICE_UUID           "fe12d210-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_RPM_UUID          "fe12d211-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_HEALTH_UUID       "fe12d212-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_LOGS_UUID         "fe12d213-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_CONTROL_UUID      "fe12d214-3d4f-4b9d-a21c-92782ecb4000"

// --- RPM Packet (UUID: ...d211) ---
// Fixed 20-byte binary packet. Properties: READ, NOTIFY.
// Rate: 5-10 Hz when connected and subscribed.
enum BleRpmStatusFlags : uint8_t {
  BLE_RPM_FLAG_CANDIDATE_FRESH     = 0x01, // Bit 0: candidate RPM fresh
  BLE_RPM_FLAG_CALIBRATION_VERIFIED= 0x02, // Bit 1: calibration verified (0 in v1)
  BLE_RPM_FLAG_RPM_MAPPED          = 0x04, // Bit 2: production RPM mapping enabled
  BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED= 0x08, // Bit 3: real RPM shift lights enabled (0 in v1)
  BLE_RPM_FLAG_CAN_HEALTHY         = 0x10, // Bit 4: CAN controller running/healthy
  BLE_RPM_FLAG_TRAFFIC_ACTIVE      = 0x20, // Bit 5: recent CAN traffic observed
  BLE_RPM_FLAG_RECORDING_ACTIVE    = 0x40, // Bit 6: flash recording actively running
  BLE_RPM_FLAG_CAPTURE_PRESERVED   = 0x80  // Bit 7: previous capture preserved
};

struct BleRpmPacket {
  uint8_t  version;          // Offset 0: Protocol version = 1
  uint8_t  statusFlags;      // Offset 1: Status flags (BleRpmStatusFlags)
  uint16_t sequenceNumber;   // Offset 2-3: Monotonic notification counter (little-endian)
  uint32_t uptimeMs;         // Offset 4-7: ESP32 uptime in ms (little-endian)
  uint16_t provisionalRpm;   // Offset 8-9: Estimated RPM; 65535 = unavailable / stale
  uint8_t  candidateRawByte; // Offset 10: Raw byte from candidate ID (valid only if fresh flag set)
  uint8_t  captureStage;     // Offset 11: Field capture stage (0=Standby, 1=KeyOn, 2=Idle, 3=GentleRPM)
  uint32_t canFramesRx;      // Offset 12-15: Total CAN frames received
  uint16_t candidateAgeMs;   // Offset 16-17: Milliseconds since candidate update; 65535 = unavailable
  uint16_t reserved;         // Offset 18-19: Reserved (0)
};

// --- Health Packet (UUID: ...d212) ---
// Fixed 20-byte binary packet. Properties: READ, NOTIFY.
// Rate: ~1 Hz when connected and subscribed.
enum BleHealthFlags : uint8_t {
  BLE_HEALTH_FLAG_STORAGE_MOUNTED = 0x01, // Bit 0: LittleFS mounted
  BLE_HEALTH_FLAG_STORAGE_FULL    = 0x02, // Bit 1: Storage full
  BLE_HEALTH_FLAG_RX_DEGRADED     = 0x04, // Bit 2: Receive performance degraded (missed/overrun/dropped > 0)
  BLE_HEALTH_FLAG_CLIENT_CONNECTED= 0x08  // Bit 3: BLE client connected
};

struct BleHealthPacket {
  uint8_t  version;          // Offset 0: Protocol version = 1
  uint8_t  healthFlags;      // Offset 1: Health flags (BleHealthFlags)
  uint16_t sequenceNumber;   // Offset 2-3: Monotonic notification counter (little-endian)
  uint32_t missedFrames;     // Offset 4-7: Driver-missed CAN frames in current session
  uint32_t overrunFrames;    // Offset 8-11: Hardware FIFO overruns in current session
  uint32_t busErrors;        // Offset 12-15: CAN bus error counter
  uint32_t recordedFrames;   // Offset 16-19: Recorded frames in current session
};

// --- Diagnostic Log Chunk Packet (UUID: ...d213) ---
// Fixed maximum 20-byte chunk. Properties: NOTIFY.
enum BleLogFlags : uint8_t {
  BLE_LOG_FLAG_FIRST_CHUNK = 0x01, // Bit 0: First chunk of message
  BLE_LOG_FLAG_FINAL_CHUNK = 0x02, // Bit 1: Final chunk of message
  BLE_LOG_FLAG_END_OF_PAGE = 0x04, // Bit 2: End-of-page marker
  BLE_LOG_FLAG_ERROR       = 0x08  // Bit 3: Error marker
};

struct BleLogChunkPacket {
  uint8_t  version;          // Offset 0: Protocol version = 1
  uint8_t  flags;            // Offset 1: Chunk flags (BleLogFlags)
  uint16_t messageId;        // Offset 2-3: Unique message ID (little-endian)
  uint8_t  chunkIndex;       // Offset 4: Zero-based chunk index
  uint8_t  payloadLength;    // Offset 5: Valid payload bytes in data (0-14)
  uint8_t  payload[14];      // Offset 6-19: Payload bytes (padded with 0)
};

// For End-of-page packet:
// payloadLength = 5
// payload[0..3] = next historical log byte offset (uint32_t little-endian)
// payload[4] = finished flag (1 = no more logs, 0 = more logs available)

// --- Control Requests (UUID: ...d214) ---
// Properties: WRITE WITH RESPONSE.
enum BleControlCommand : uint8_t {
  BLE_CTRL_CMD_GET_LOG_PAGE = 0x10, // [0x01, 0x10, offset_u32_LE] (6 bytes)
  BLE_CTRL_CMD_CANCEL_LOGS  = 0x11  // [0x01, 0x11] (2 bytes)
};

#pragma pack(pop)

static_assert(sizeof(BleRpmPacket) == 20, "BleRpmPacket must be exactly 20 bytes");
static_assert(sizeof(BleHealthPacket) == 20, "BleHealthPacket must be exactly 20 bytes");
static_assert(sizeof(BleLogChunkPacket) == 20, "BleLogChunkPacket must be exactly 20 bytes");
