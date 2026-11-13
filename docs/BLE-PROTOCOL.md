# PulseShift BLE Protocol Specification (v1 & v2)

**Document Version:** 2.0.0  
**Target Hardware:** ESP32 DevKit V1 (ESP32-D0WD-V3)  
**Companion App:** PulseShift Android App (`D:\Development\PulseShift\android`)  
**Firmware Library:** NimBLE-Arduino 2.3.8  
**Arduino-ESP32 Core:** 3.3.12 (ESP-IDF 5.3+)

---

## 1. Overview and Architecture

PulseShift BLE Protocol defines the Bluetooth Low Energy GATT communication between the PulseShift ESP32 motorcycle firmware and the Android smartphone application.

### Key Tenets
1. **Unidirectional CAN Safety:** The ESP32 is a passive, receive-only automotive monitor (SN65HVD230 CTX tied to 3V3, GPIO17 air-gapped, listen-only TWAI mode). BLE configuration changes modify only local LED rendering, RPM thresholds, shift alerts, animations, and candidate monitoring parameters. BLE **never** injects frames into the vehicle CAN bus or executes destructive storage operations.
2. **Fixed 20-Byte Wire MTU:** Every characteristic command, response, and telemetry notification is constrained to exactly 20 bytes. No packet exceeds the universal BLE 4.0 ATT MTU limit (23 bytes total - 3 bytes ATT header = 20 bytes payload). No MTU renegotiation or L2CAP segmentation is required.
3. **Little-Endian Architecture:** All multi-byte integers (`uint16_t`, `uint32_t`, `float`) are serialized in standard IEEE 754 little-endian byte ordering.
4. **Resilient and Decoupled:** The ESP32 firmware functions autonomously without an active BLE connection. Telemetry notifications are sent only to subscribed GATT clients. Disconnections automatically re-arm advertising without interrupting CAN reception.
5. **NVS Persistence and Staged Transactions:** Changes can be applied immediately, staged across atomic multi-parameter transactions, and saved to non-volatile storage (NVS) using CRC32 checksums.
6. **Defensible Physical Owner Security:** Write operations require physical presence proof via a 60-second authorization window triggered by the physical BOOT button on the ESP32.

---

## 2. GATT Service and Characteristics

**Advertised Device Name:** `PulseShift`  
**Primary Service UUID:** `fe12d210-3d4f-4b9d-a21c-92782ecb4000`

| Purpose | Full UUID | Properties | Frequency / Access | Description |
|---|---|---|---|---|
| **RPM Telemetry** | `fe12d211-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 10 Hz (100 ms) | Live engine speed, raw candidate byte, age, CAN frame count |
| **System Health** | `fe12d212-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 1 Hz (1000 ms) | CAN bus health, missed/overrun counters, storage status |
| **Diagnostic Logs** | `fe12d213-3d4f-4b9d-a21c-92782ecb4000` | `NOTIFY` | Event / Paced (20 ms) | Live diagnostic events and paginated historical logs |
| **Control Requests (v1)**| `fe12d214-3d4f-4b9d-a21c-92782ecb4000` | `WRITE` | On-demand | Diagnostic queries (e.g. GET_LOG_PAGE, CANCEL_LOGS) |
| **Config Command (v2)** | `fe12d215-3d4f-4b9d-a21c-92782ecb4000` | `WRITE` | On-demand | Configuration write/read requests and transaction controls |
| **Config Result (v2)** | `fe12d216-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | Immediate response | Status code, active revision counter, and result flags |
| **Config Snapshot (v2)**| `fe12d217-3d4f-4b9d-a21c-92782ecb4000` | `NOTIFY` | Paced (20 ms stream) | Multi-chunk serialized configuration TLV blob with CRC32 |
| **Capabilities (v2)** | `fe12d218-3d4f-4b9d-a21c-92782ecb4000` | `READ` | Static / On revision | Hardware capabilities, supported effects, feature mask |

---

## 3. Protocol v1 Telemetry Packets

### 3.1 RPM Telemetry Packet (`0xd211`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `statusFlags` | `uint8_t` | Bitmask representing live subsystem states (see Section 3.1.1) |
| 2..3 | `sequenceNumber` | `uint16_t` | Incremental rolling packet sequence counter (0..65535) |
| 4..7 | `uptimeMs` | `uint32_t` | ESP32 monotonic uptime in milliseconds (`millis()`) |
| 8..9 | `provisionalRpm` | `uint16_t` | Engine RPM calculated as `candidateRawByte * multiplier`. `65535` = unavailable/stale |
| 10 | `candidateRawByte` | `uint8_t` | Raw byte value extracted from candidate CAN ID |
| 11 | `captureStage` | `uint8_t` | Current field stage: `0`=Unset, `1`=KeyOn, `2`=Idling, `3`=GentleRPM |
| 12..15 | `canFramesRx` | `uint32_t` | Cumulative count of total CAN frames received by TWAI driver |
| 16..17 | `candidateAgeMs` | `uint16_t` | Milliseconds elapsed since last candidate frame. `65535` = never received |
| 18..19 | `reserved` | `uint16_t` | Reserved for alignment; always zero |

#### 3.1.1 Status Flags Bitmask (`statusFlags`)
- `Bit 0` (`0x01`): **Candidate Fresh** - Candidate frame received within freshness window (250 ms).
- `Bit 1` (`0x02`): **Calibration Verified** - Physical tachometer validation completed.
- `Bit 2` (`0x04`): **RPM Mapped** - Vehicle profile RPM arbitration ID mapped.
- `Bit 3` (`0x08`): **Shift Lights Enabled** - Physical shift lights active and verified.
- `Bit 4` (`0x10`): **CAN Healthy** - ESP-IDF TWAI controller is operational (not in bus-off/halted).
- `Bit 5` (`0x20`): **Recent CAN Traffic** - At least one frame received in the last 1000 ms.
- `Bit 6` (`0x40`): **Flash Recording Active** - Actively buffering/writing frames to LittleFS.
- `Bit 7` (`0x80`): **Capture Preserved** - Previous session capture is preserved on flash.

---

### 3.2 System Health Packet (`0xd212`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `healthFlags` | `uint8_t` | Health status flags (see Section 3.2.1) |
| 2..3 | `sequenceNumber` | `uint16_t` | Incremental rolling packet sequence counter (0..65535) |
| 4..7 | `missedFrames` | `uint32_t` | Cumulative driver-missed frames (TWAI software queue full) |
| 8..11 | `overrunFrames` | `uint32_t` | Cumulative hardware FIFO overrun frames |
| 12..15 | `busErrors` | `uint32_t` | Cumulative hardware bus error events |
| 16..19 | `recordedFrames` | `uint32_t` | Cumulative records written to LittleFS flash capture |

#### 3.2.1 Health Flags Bitmask (`healthFlags`)
- `Bit 0` (`0x01`): **Storage Mounted** - LittleFS filesystem mounted successfully.
- `Bit 1` (`0x02`): **Storage Full** - Flash capture reached quota or storage full.
- `Bit 2` (`0x04`): **RX Degraded** - Hardware or driver dropped frames (`missed > 0` or `overruns > 0`).
- `Bit 3` (`0x08`): **Client Connected** - A BLE central client is connected.

---

### 3.3 Diagnostic Log Chunk Packet (`0xd213`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `flags` | `uint8_t` | Chunk control flags: Bit 0=FIRST_CHUNK, Bit 1=FINAL_CHUNK, Bit 2=END_OF_PAGE |
| 2..3 | `messageId` | `uint16_t` | Unique message identifier for this log line |
| 4 | `chunkIndex` | `uint8_t` | 0-based index of this chunk within the message |
| 5 | `payloadLength` | `uint8_t` | Number of valid payload bytes in `payload` (1..14) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Raw ASCII log text chunk, or End-of-Page metadata |

---

## 4. Protocol v2 Configuration Architecture

### 4.1 Device Capabilities (`0xd218`, READ, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1 | `physicalLedCount`| `uint8_t` | Physical LED count = `6` |
| 2..5 | `featureMask` | `uint32_t` | Feature bitmask = `0x03FF` (1023, all 10 features supported) |
| 6..9 | `currentRevision` | `uint32_t` | Monotonic active configuration revision counter |
| 10 | `presetCapacity` | `uint8_t` | Hardware preset slots (`0` = smartphone-managed presets) |
| 11 | `securityMode` | `uint8_t` | Security mode: `0`=Open, `1`=ButtonWindow, `2`=Token, `3`=Hybrid |
| 12..19 | `reserved[8]` | `uint8_t[8]` | Reserved for future expansion; all zeros |

#### Feature Mask Bits (`featureMask`)
- `Bit 0` (`0x0001`): `FEATURE_PERSISTENT_NVS`
- `Bit 1` (`0x0002`): `FEATURE_STAGED_TRANSACTIONS`
- `Bit 2` (`0x0004`): `FEATURE_CALIBRATION_SAFETY`
- `Bit 3` (`0x0008`): `FEATURE_PHYSICAL_AUTH`
- `Bit 4` (`0x0010`): `FEATURE_CUSTOM_ANIMATIONS`
- `Bit 5` (`0x0020`): `FEATURE_SHIFT_WARNING`
- `Bit 6` (`0x0040`): `FEATURE_REVERSIBLE_DIRECTION`
- `Bit 7` (`0x0080`): `FEATURE_INDIVIDUAL_LEDS`
- `Bit 8` (`0x0100`): `FEATURE_HISTORICAL_LOGS`
- `Bit 9` (`0x0200`): `FEATURE_CAN_PASSIVE_LISTEN`

---

### 4.2 Configuration Command Packet (`0xd215`, WRITE, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1 | `opcode` | `uint8_t` | Command opcode (see Section 4.4) |
| 2..3 | `sequence` | `uint16_t` | Client request sequence counter (echoed in Result) |
| 4 | `payloadLength` | `uint8_t` | Number of valid parameter bytes in `payload` (0..14) |
| 5 | `flags` | `uint8_t` | Bit 0: `IS_STAGED` (1 = stage in active transaction, 0 = apply live) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Command arguments |

---

### 4.3 Configuration Result Packet (`0xd216`, READ/NOTIFY, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1 | `respondedOpcode`| `uint8_t` | Opcode being acknowledged |
| 2..3 | `sequence` | `uint16_t` | Client sequence counter echoed from request |
| 4 | `statusCode` | `uint8_t` | Execution status (0 = OK, see Section 4.5) |
| 5 | `resultFlags` | `uint8_t` | Result flags (Bit 0=Persisted, Bit 1=Staged, Bit 2=Applied, Bit 3=AuthRequired) |
| 6..9 | `activeRevision` | `uint32_t` | Active configuration revision after command execution |
| 10 | `payloadLength` | `uint8_t` | Number of response bytes in `payload` (0..9) |
| 11..19 | `payload[9]` | `uint8_t[9]` | Optional response data |

---

### 4.4 Command Opcodes

| Opcode | Name | Payload Arguments | Description |
|---|---|---|---|
| `0x01` | `GET_CAPABILITIES` | None | Updates Capabilities characteristic |
| `0x02` | `GET_CONFIG` | None | Triggers complete snapshot streaming on `0xd217` |
| `0x10` | `SET_POWER` | `[power: u8 (0 or 1)]` | Master LED strip power switch |
| `0x11` | `SET_BRIGHTNESS` | `[brightness: u8 (0..255)]` | Master LED brightness |
| `0x12` | `SET_GLOBAL_COLOR`| `[R: u8, G: u8, B: u8]` | Set uniform color across all 6 LEDs |
| `0x13` | `SET_LED_COLOR` | `[index: u8 (0..5), R, G, B]` | Set individual LED color |
| `0x14` | `SET_EFFECT` | `[effectId: u8 (0..4), speed: u8 (0..100)]` | 0=Static, 1=Breathing, 2=Rainbow, 3=Chase, 4=RPM_Bar |
| `0x15` | `SET_STARTUP_ANIM` | `[animId: u8 (0..3), durationMs: u16 LE]` | 0=None, 1=Sweep, 2=Wave, 3=Pulse |
| `0x16` | `SET_RPM_THRESHOLD`| `[index: u8 (0..5), rpm: u16 LE]` | Set threshold for LED index (must remain monotonic) |
| `0x17` | `SET_SHIFT_WARNING`| `[warningRpm: u16 LE, patternId: u8]` | Pattern: 0=Flash (10 Hz), 1=Strobe (20 Hz), 2=Solid, 3=Off |
| `0x18` | `SET_RPM_MULTIPLIER`| `[multX1000: u32 LE]` | Provisional multiplier (0.1..1000.0). **Invalidates calibration!** |
| `0x19` | `SET_CAN_CANDIDATE` | `[canId: u32 LE, byteIdx: u8, ext: u8]` | Target CAN candidate source. **Invalidates calibration!** |
| `0x1A` | `SET_LED_DIRECTION`| `[direction: u8 (0=Forward, 1=Reverse)]`| Shift light display direction |
| `0x1B` | `SET_WARNING_COLOR`| `[R: u8, G: u8, B: u8]` | Warning color for over-rev alert |
| `0x20` | `BEGIN_TRANSACTION`| None | Start staged transaction (30s timeout) |
| `0x21` | `COMMIT_TRANSACTION`| None | Validate all staged changes and apply atomically |
| `0x22` | `ABORT_TRANSACTION` | None | Discard all staged changes without modifying active config |
| `0x30` | `SAVE_TO_NVS` | None | Commit active configuration to flash NVS (`"ps_cfg"`) |
| `0x31` | `RESTORE_DEFAULTS` | `[0xA5, 0x5A]` | Factory reset configuration and persist to NVS |
| `0x40` | `REQ_AUTH_CHALLENGE`| None | Request authentication nonce |
| `0x41` | `AUTHENTICATE` | `[token: u8[4..8]]` | Authenticate connection session |

---

### 4.5 Status Codes

| Code | Name | Description |
|---|---|---|
| `0x00` | `BLE_CFG_STATUS_OK` | Command executed successfully |
| `0x01` | `BLE_CFG_STATUS_INVALID_PARAM` | Parameter out of range or non-monotonic |
| `0x02` | `BLE_CFG_STATUS_UNAUTHORIZED` | Authorization required; press physical BOOT button |
| `0x03` | `BLE_CFG_STATUS_BUSY` | Subsystem busy processing another request |
| `0x04` | `BLE_CFG_STATUS_CRC_ERROR` | Checksum error |
| `0x05` | `BLE_CFG_STATUS_UNSUPPORTED` | Opcode or parameter unsupported |
| `0x06` | `BLE_CFG_STATUS_NVS_ERROR` | Flash NVS write failed |
| `0x07` | `BLE_CFG_STATUS_TX_ABORTED` | Transaction timed out or aborted |
| `0x08` | `BLE_CFG_STATUS_INVALID_STATE` | Staged command outside transaction, or RPM_Bar when unverified |

---

### 4.6 Configuration Snapshot (`0xd217`, NOTIFY, 20-Byte Chunks)

Streamed with 20 ms pacing between chunks to prevent Bluetooth buffer congestion.

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1..2 | `snapshotId` | `uint16_t` | Snapshot identifier (little-endian) |
| 3 | `chunkIndex` | `uint8_t` | 0-based chunk sequence index |
| 4 | `chunkCount` | `uint8_t` | Total chunks comprising the snapshot |
| 5 | `payloadLength` | `uint8_t` | Number of valid bytes in this chunk (1..14) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Serialized snapshot TLV fragment |

#### Reassembled Snapshot Binary Layout
```text
[Byte 0]     : schemaVersion (uint8_t = 2)
[Bytes 1..4] : activeRevision (uint32_t LE)
[Bytes 5..N-4]: TLV Records (see Section 4.7)
[Bytes N-3..N]: CRC32 (uint32_t LE, IEEE 802.3 over Bytes 0..N-4)
```

---

### 4.7 Snapshot TLV Format

Every TLV record is formatted as `[Tag: uint8_t, Length: uint8_t, Value: uint8_t[Length]]`.

All 12 mandatory tags (`0x10` through `0x1B`) are emitted:

| Tag | Name | Length | Value Format | Description |
|---|---|---|---|---|
| `0x10` | Power | 1 | `[power: u8]` | `0` = Off, `1` = On |
| `0x11` | Brightness | 1 | `[brightness: u8]` | Master brightness `0..255` |
| `0x12` | Global Color | 3 | `[R, G, B]` | Primary single RGB color |
| `0x13` | Per-LED Colors | 18 | `[6 * (R, G, B)]` | Individual RGB colors for LEDs 0..5 |
| `0x14` | Effect & Speed | 2 | `[effectId: u8, speed: u8]` | Effect (0..4), Speed (0..100) |
| `0x15` | Startup Animation | 3 | `[animId: u8, durationMs: u16 LE]` | Startup anim (0..3), duration in ms |
| `0x16` | RPM Thresholds | 12 | `[6 * u16 LE]` | Monotonic thresholds for LEDs 0..5 |
| `0x17` | Shift Warning | 3 | `[warningRpm: u16 LE, patternId: u8]`| Warning threshold and pattern |
| `0x18` | RPM Multiplier | 4 | `[multX1000: u32 LE]` | Multiplier * 1000 (e.g. 40.0 -> 40000) |
| `0x19` | CAN Candidate | 6 | `[canId: u32 LE, byteIdx: u8, ext: u8]`| Target candidate CAN source |
| `0x1A` | LED Direction | 1 | `[direction: u8]` | `0` = Forward (0->5), `1` = Reverse (5->0) |
| `0x1B` | Warning Color | 3 | `[R, G, B]` | Alert over-rev flash color |

---

## 5. Security and Physical Authorization

The PulseShift hardware is a dedicated headless embedded unit without a screen or keyboard. To defend against unauthorized vehicle tuning or drive-by configuration tampering, PulseShift enforces physical presence proof:

1. **Physical BOOT Button Window:**
   - Pressing the physical BOOT button (GPIO0) opens a **60-second authorization window**.
   - During the authorization window, the LED strip pulses **cyan** (unless higher-priority safety alerts are active).
   - Configuration writes arriving while the window is active are permitted and recorded in the diagnostic event log.
2. **Unauthorized Write Rejection:**
   - When the authorization window is closed, write commands to opcodes `0x10`-`0x31` are rejected with `BLE_CFG_STATUS_UNAUTHORIZED` (`0x02`) and result flag `BLE_CFG_RESULT_FLAG_AUTH_REQUIRED` (`0x08`).
   - The Android app receives this status code and prompts the rider: *"Press the BOOT button on the PulseShift hardware to authorize changes."*
3. **Session Token Support:**
   - central clients can authenticate with a 4-byte token via opcode `0x41` during an open window to maintain session authorization across subsequent requests.
