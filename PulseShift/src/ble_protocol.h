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

// Configuration V2 Characteristics (Protocol Version 2)
#define PULSESHIFT_BLE_CHAR_CONFIG_CMD_UUID      "fe12d215-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_CONFIG_RESULT_UUID   "fe12d216-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_CONFIG_SNAPSHOT_UUID "fe12d217-3d4f-4b9d-a21c-92782ecb4000"
#define PULSESHIFT_BLE_CHAR_CAPABILITIES_UUID    "fe12d218-3d4f-4b9d-a21c-92782ecb4000"

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
  BLE_LOG_FLAG_FIRST_CHUNK  = 0x01, // Bit 0: First chunk of message
  BLE_LOG_FLAG_FINAL_CHUNK  = 0x02, // Bit 1: Final chunk of message
  BLE_LOG_FLAG_END_OF_PAGE  = 0x04, // Bit 2: End-of-page marker
  BLE_LOG_FLAG_ERROR        = 0x08, // Bit 3: Error marker
  BLE_LOG_FLAG_CAPTURE_LINE = 0x10  // Bit 4: CAN capture record line (CSV like serial DUMP)
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
// (BleControlCommand is defined together with the v2 configuration opcodes
//  below so capture-browsing commands stay in one versioned block.)

// ============================================================================
// PulseShift BLE Protocol v2 Definitions (Configuration & Control)
// ============================================================================

#define PULSESHIFT_BLE_CONFIG_PROTOCOL_VERSION 2

// Firmware version reported in the Capabilities packet (schema 3)
#define PULSESHIFT_FW_VERSION_MAJOR 2
#define PULSESHIFT_FW_VERSION_MINOR 2
#define PULSESHIFT_FW_VERSION_PATCH 0

// Configuration Opcodes
enum BleConfigOpcode : uint8_t {
  BLE_CFG_OP_GET_CAPABILITIES    = 0x01,
  BLE_CFG_OP_GET_SNAPSHOT        = 0x02,
  BLE_CFG_OP_SET_POWER           = 0x10,
  BLE_CFG_OP_SET_BRIGHTNESS      = 0x11,
  BLE_CFG_OP_SET_GLOBAL_RGB      = 0x12,
  BLE_CFG_OP_SET_LED_RGB         = 0x13,
  BLE_CFG_OP_SET_EFFECT          = 0x14,
  BLE_CFG_OP_SET_STARTUP         = 0x15,
  BLE_CFG_OP_SET_LED_THRESHOLD   = 0x16,
  BLE_CFG_OP_SET_SHIFT_WARNING   = 0x17,
  BLE_CFG_OP_SET_RPM_MULTIPLIER  = 0x18,
  BLE_CFG_OP_SET_CANDIDATE_SOURCE= 0x19,
  BLE_CFG_OP_SET_LED_DIRECTION   = 0x1A,
  BLE_CFG_OP_SET_WARNING_RGB     = 0x1B,
  BLE_CFG_OP_SET_RPM_HYSTERESIS  = 0x1C, // [hyst_u16_LE] (schema 3)
  BLE_CFG_OP_SET_RPM_DECODER_EXT = 0x1D, // [width, endian, offset_s32LE(×1000), maxValid_u16LE] (8 bytes)
  BLE_CFG_OP_SAVE_CONFIG         = 0x20,
  BLE_CFG_OP_RESTORE_DEFAULTS    = 0x21,
  BLE_CFG_OP_RELOAD_CONFIG       = 0x22, // re-read NVS blob and apply (auth required)
  BLE_CFG_OP_ENROLL_OWNER        = 0x24,
  BLE_CFG_OP_AUTHENTICATE        = 0x25,
  BLE_CFG_OP_REQUEST_CHALLENGE   = 0x26,
  BLE_CFG_OP_BEGIN_TRANSACTION   = 0x30,
  BLE_CFG_OP_COMMIT_TRANSACTION  = 0x31,
  BLE_CFG_OP_ABORT_TRANSACTION   = 0x32,
  // --- Mobile-assisted RPM calibration workflow (schema 3) ---
  BLE_CFG_OP_CAL_BEGIN           = 0x40, // start calibration session
  BLE_CFG_OP_CAL_ABORT           = 0x41, // abort session
  BLE_CFG_OP_CAL_CAPTURE_START   = 0x42, // open 2 s raw sampling window
  BLE_CFG_OP_CAL_CAPTURE_STATUS  = 0x43, // query window: [samples u16LE, elapsedMs u16LE]
  BLE_CFG_OP_CAL_POINT_COMMIT    = 0x44, // [refRpm u16LE] close window + store point
  BLE_CFG_OP_CAL_COMPUTE         = 0x45, // [apply u8] fit + validate; payload returns fit
  BLE_CFG_OP_CAL_STATUS          = 0x46, // query session: [active, points, captureActive, samples u16LE]
  // --- Device control & diagnostics (protocol 2.2.0) ---
  BLE_CFG_OP_GET_DIAGNOSTICS     = 0x50, // stream diagnostic TLV blob via snapshot char (read-only)
  BLE_CFG_OP_RESTART_DEVICE      = 0x51, // [0xA5, 0x5A] explicit confirmation; ESP32 soft restart
  BLE_CFG_OP_CLEAR_DIAGNOSTICS   = 0x52, // [0xA5, 0x5A] explicit confirmation; clears diag log ONLY (captures untouched)
  BLE_CFG_OP_SET_CAPTURE_RECORDING = 0x53 // [enable u8] start/stop flash recording (auth required)
};

// v1 Control extensions (CAN capture browsing, bounded + cancellable)
enum BleControlCommand : uint8_t {
  BLE_CTRL_CMD_GET_LOG_PAGE = 0x10, // [0x01, 0x10, offset_u32_LE] (6 bytes)
  BLE_CTRL_CMD_CANCEL_LOGS  = 0x11, // [0x01, 0x11] (2 bytes)
  BLE_CTRL_CMD_GET_CAPTURE_PAGE = 0x12, // [0x01, 0x12, startRecord_u32_LE] (6 bytes)
  BLE_CTRL_CMD_CANCEL_CAPTURE   = 0x13  // [0x01, 0x13] (2 bytes)
};

// Configuration Command Flags
enum BleConfigCommandFlags : uint8_t {
  BLE_CFG_FLAG_STAGED = 0x01 // Bit 0: Update applies to staged transaction copy
};

// Configuration Status Codes
enum BleConfigStatusCode : uint8_t {
  BLE_CFG_STATUS_OK                 = 0,
  BLE_CFG_STATUS_INVALID_PARAM      = 1,
  BLE_CFG_STATUS_UNSUPPORTED        = 2,
  BLE_CFG_STATUS_BUSY               = 3,
  BLE_CFG_STATUS_UNAUTHORIZED       = 4,
  BLE_CFG_STATUS_INVALID_STATE      = 5,
  BLE_CFG_STATUS_STORAGE_ERROR      = 6,
  BLE_CFG_STATUS_VERSION_MISMATCH   = 7,
  BLE_CFG_STATUS_VALIDATION_FAILED  = 8
};

// Configuration Result Flags
enum BleConfigResultFlags : uint8_t {
  BLE_CFG_RESULT_FLAG_PERSISTED     = 0x01, // Bit 0: Saved to persistent NVS storage
  BLE_CFG_RESULT_FLAG_STAGED        = 0x02, // Bit 1: Staged only in transaction buffer
  BLE_CFG_RESULT_FLAG_APPLIED       = 0x04, // Bit 2: Applied to active live state
  BLE_CFG_RESULT_FLAG_AUTH_REQUIRED = 0x08  // Bit 3: Authentication required for write
};

// Feature Mask Bits (Capabilities)
enum BleFeatureMaskBits : uint32_t {
  BLE_FEAT_INDIVIDUAL_RGB   = (1u << 0),
  BLE_FEAT_EFFECTS          = (1u << 1),
  BLE_FEAT_STARTUP_ANIM     = (1u << 2),
  BLE_FEAT_RPM_THRESHOLDS   = (1u << 3),
  BLE_FEAT_SHIFT_WARNING    = (1u << 4),
  BLE_FEAT_RPM_MULTIPLIER   = (1u << 5),
  BLE_FEAT_CANDIDATE_SOURCE = (1u << 6),
  BLE_FEAT_PHONE_PRESETS    = (1u << 7),
  BLE_FEAT_NVS_PERSISTENCE  = (1u << 8),
  BLE_FEAT_AUTH_WRITES      = (1u << 9),
  // Schema-3 extensions (unified RPM pipeline + calibration workflow)
  BLE_FEAT_DECODER_EXT      = (1u << 10), // width/endianness/offset/clamp
  BLE_FEAT_HYSTERESIS       = (1u << 11), // threshold hysteresis
  BLE_FEAT_CAL_WORKFLOW     = (1u << 12), // mobile-assisted calibration session
  BLE_FEAT_DIAGNOSTICS_TLV  = (1u << 13), // GET_DIAGNOSTICS TLV blob
  BLE_FEAT_CAPTURE_EXPORT   = (1u << 14)  // bounded CAN capture pages over BLE
};

// BLE_RPM packet extended flags usage (schema 3): Bit 1 additionally requires
// the verification record to be BOUND to the current decoder fingerprint;
// Bit 3 is only set when RPM-driven lighting is actually rendering.

// Provisioning / Security Modes
enum BleSecurityMode : uint8_t {
  BLE_SEC_MODE_OPEN          = 0,
  BLE_SEC_MODE_BUTTON_WINDOW = 1,
  BLE_SEC_MODE_TOKEN         = 2,
  BLE_SEC_MODE_HYBRID        = 3 // Physical button presence OR device PIN/token
};

// Warning Pattern IDs
enum BleWarningPattern : uint8_t {
  BLE_WARN_PATTERN_FLASH     = 0, // Fast Flash (10 Hz)
  BLE_WARN_PATTERN_STROBE    = 1, // Strobe Blink (20 Hz)
  BLE_WARN_PATTERN_SOLID     = 2, // Solid Over-rev
  BLE_WARN_PATTERN_OFF       = 3  // Shift alert off
};

// Binary Command Packet Header (UUID: ...d215, max 20 bytes)
struct BleConfigCommandPacket {
  uint8_t  version;          // Offset 0: Protocol version = 2
  uint8_t  opcode;           // Offset 1: Opcode (BleConfigOpcode)
  uint16_t sequence;         // Offset 2-3: Request sequence (little-endian)
  uint8_t  payloadLength;    // Offset 4: Payload length (0..14)
  uint8_t  flags;            // Offset 5: Flags (Bit 0 = staged)
  uint8_t  payload[14];      // Offset 6-19: Command arguments
};

// Binary Command Result Packet (UUID: ...d216, exactly 20 bytes)
struct BleConfigResultPacket {
  uint8_t  version;          // Offset 0: Protocol version = 2
  uint8_t  respondedOpcode;  // Offset 1: Responded opcode
  uint16_t sequence;         // Offset 2-3: Request sequence reference (little-endian)
  uint8_t  statusCode;       // Offset 4: Status code (BleConfigStatusCode)
  uint8_t  resultFlags;      // Offset 5: Result flags (BleConfigResultFlags)
  uint32_t activeRevision;   // Offset 6-9: Active configuration revision (little-endian)
  uint8_t  payloadLength;    // Offset 10: Valid payload length (0..9)
  uint8_t  payload[9];       // Offset 11-19: Optional result bytes
};

// Binary Config Snapshot Chunk Packet (UUID: ...d217, exactly 20 bytes)
struct BleConfigSnapshotChunkPacket {
  uint8_t  version;          // Offset 0: Protocol version = 2
  uint16_t snapshotId;       // Offset 1-2: Snapshot ID (little-endian; request sequence or 0xFFFF)
  uint8_t  chunkIndex;       // Offset 3: 0-based chunk index
  uint8_t  chunkCount;       // Offset 4: Total chunks in snapshot
  uint8_t  payloadLength;    // Offset 5: Valid payload length in chunk (0..14)
  uint8_t  payload[14];      // Offset 6-19: Serialized TLV configuration chunk
};

// Diagnostics blob TLV IDs (streamed via the snapshot characteristic by
// BLE_CFG_OP_GET_DIAGNOSTICS). Body layout: TLV stream + trailing CRC32
// (identical framing to the configuration snapshot; first byte is a schema
// version tag = 1 for diagnostics blobs, followed by revision u32 = 0).
enum BleDiagnosticsTlvId : uint8_t {
  BLE_DIAG_TLV_UPTIME        = 0x40, // u32 LE: uptime ms
  BLE_DIAG_TLV_FIRMWARE      = 0x41, // [major, minor, patch]
  BLE_DIAG_TLV_RESET_REASON  = 0x42, // u8: esp_reset_reason of the last boot
  BLE_DIAG_TLV_CAN_STATUS    = 0x43, // bitrate u32, totalRx u32, accepted u32,
                                     // missed u32, overrun u32, busErrors u32,
                                     // healthy u8, traffic u8 (26 bytes)
  BLE_DIAG_TLV_CANDIDATE     = 0x44, // canId u32, byteIndex u8, byteWidth u8,
                                     // extended u8, multiplier x1000 s32 LE,
                                     // offset x1000 s32 LE, maxValid u16 LE,
                                     // raw u16, estimatedRpm u16, fresh u8,
                                     // valid u8, calVerified u8, ageMs u16,
                                     // received u32, rejected u32 (34 bytes)
  BLE_DIAG_TLV_STORAGE       = 0x45, // total u32, used u32, free u32, mounted u8,
                                     // storageFull u8, capturePreserved u8,
                                     // damaged u8, recording u8, records u32,
                                     // captureSize u32, diagLogSize u32 (29 bytes)
  BLE_DIAG_TLV_BLE_STATE     = 0x46, // connected u8, advertising u8, securityMode u8,
                                     // ownerEnrolled u8, sessionAuthorized u8 (5 bytes)
  BLE_DIAG_TLV_CAPTURE_INFO  = 0x47, // stage u8, sessionId u32, bootCount u32 (9 bytes)
  BLE_DIAG_TLV_LAST_ERROR    = 0x48, // UTF-8 string (<= 96 bytes)
  BLE_DIAG_TLV_CALIBRATION   = 0x49  // sessionActive u8, points u8, captureActive u8,
                                     // samples u16 LE, calVerified u8, pointsUsed u8,
                                     // maxResidual u8, fingerprint u32 LE (13 bytes)
};

// Binary Capabilities Packet (UUID: ...d218, exactly 20 bytes)
struct BleCapabilitiesPacket {
  uint8_t  version;                // Byte 0: Configuration protocol version = 2
  uint8_t  physicalLedCount;       // Byte 1: Physical LED count (6)
  uint32_t featureMask;            // Bytes 2-5: Feature capability mask (little-endian)
  uint32_t currentRevision;        // Bytes 6-9: Current active configuration revision (little-endian)
  uint8_t  hardwarePresetCapacity; // Byte 10: Hardware preset storage capacity (0 = phone-managed)
  uint8_t  securityMode;           // Byte 11: Security / Provisioning mode
  uint8_t  firmwareMajor;          // Byte 12: Firmware version major
  uint8_t  firmwareMinor;          // Byte 13: Firmware version minor
  uint8_t  firmwarePatch;          // Byte 14: Firmware version patch
  uint8_t  reserved[5];            // Bytes 15-19: Reserved (0)
};

#pragma pack(pop)

static_assert(sizeof(BleRpmPacket) == 20, "BleRpmPacket must be exactly 20 bytes");
static_assert(sizeof(BleHealthPacket) == 20, "BleHealthPacket must be exactly 20 bytes");
static_assert(sizeof(BleLogChunkPacket) == 20, "BleLogChunkPacket must be exactly 20 bytes");
static_assert(sizeof(BleConfigCommandPacket) == 20, "BleConfigCommandPacket must be exactly 20 bytes");
static_assert(sizeof(BleConfigResultPacket) == 20, "BleConfigResultPacket must be exactly 20 bytes");
static_assert(sizeof(BleConfigSnapshotChunkPacket) == 20, "BleConfigSnapshotChunkPacket must be exactly 20 bytes");
static_assert(sizeof(BleCapabilitiesPacket) == 20, "BleCapabilitiesPacket must be exactly 20 bytes");
