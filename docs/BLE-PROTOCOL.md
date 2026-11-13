# PulseShift BLE Protocol v1 Specification

**Document Version:** 1.0.0  
**Target Hardware:** ESP32 DevKit V1 (ESP32-D0WD-V3)  
**Companion App:** PulseShift Android App (`D:\Development\PulseShift`)  
**Firmware Library:** NimBLE-Arduino 2.3.8  
**Arduino-ESP32 Core:** 3.3.12 (ESP-IDF 5.3+)

---

## 1. Overview and Architecture

PulseShift BLE Protocol v1 defines the Bluetooth Low Energy GATT communication between the PulseShift ESP32 motorcycle firmware and the Android companion application.

### Key Tenets
1. **Unidirectional Control Safety:** The ESP32 is a receive-only automotive monitor. Control requests over BLE are strictly read-only diagnostics (requesting diagnostic log pages or acknowledging receipts). BLE **never** injects frames into the vehicle CAN bus or executes destructive storage operations.
2. **Fixed 20-Byte Wire MTU:** Every characteristic notification is exactly 20 bytes long. No packet exceeds the universal BLE 4.0 ATT MTU limit (23 bytes total - 3 bytes ATT header = 20 bytes payload). No fragmentation at the L2CAP layer is required.
3. **Little-Endian Architecture:** All multi-byte integers (`uint16_t`, `uint32_t`) are serialized in standard IEEE little-endian byte ordering.
4. **Resilient and Decoupled:** The ESP32 firmware functions autonomously without an active BLE connection. Telemetry notifications are sent only to subscribed GATT clients. Disconnections automatically re-arm advertising without interrupting CAN reception.

---

## 2. GATT Service and Characteristics

**Advertised Device Name:** `PulseShift`  
**Primary Service UUID:** `fe12d210-3d4f-4b9d-a21c-92782ecb4000`

| Purpose | Full UUID | Properties | Frequency | Description |
|---|---|---|---|---|
| **RPM Telemetry** | `fe12d211-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 10 Hz (100 ms) | Live engine speed, raw candidate byte, age, CAN frame count |
| **System Health** | `fe12d212-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 1 Hz (1000 ms) | CAN bus health, missed/overrun counters, storage status |
| **Diagnostic Logs** | `fe12d213-3d4f-4b9d-a21c-92782ecb4000` | `NOTIFY` | Event-driven / Rate-limited | Live diagnostic events and paginated historical logs |
| **Control Requests** | `fe12d214-3d4f-4b9d-a21c-92782ecb4000` | `WRITE` | On-demand | Client diagnostic queries (e.g. request next log page) |

---

## 3. Binary Packet Structures (Exact 20 Bytes)

### 3.1 RPM Telemetry Packet (`0xd211`)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `statusFlags` | `uint8_t` | Bitmask representing live subsystem states (see Section 3.1.1) |
| 2..3 | `sequenceNumber` | `uint16_t` | Incremental rolling packet sequence counter (0..65535) |
| 4..7 | `uptimeMs` | `uint32_t` | ESP32 monotonic uptime in milliseconds (`millis()`) |
| 8..9 | `provisionalRpm` | `uint16_t` | Engine RPM calculated as `candidateRawByte * multiplier`. `65535` = unavailable/stale |
| 10 | `candidateRawByte` | `uint8_t` | Raw byte value extracted from candidate ID 0x301, byte index 0 |
| 11 | `captureStage` | `uint8_t` | Current field stage: `0`=Unset, `1`=KeyOn/EngineOff, `2`=EngineIdling, `3`=GentleRPM |
| 12..15 | `canFramesRx` | `uint32_t` | Cumulative count of total CAN frames received by TWAI driver |
| 16..17 | `candidateAgeMs` | `uint16_t` | Milliseconds elapsed since last fresh frame on 0x301. `65535` = never received |
| 18..19 | `reserved` | `uint16_t` | Reserved for alignment and future extension; always zero |

#### 3.1.1 Status Flags Bitmask (`statusFlags`)
- `Bit 0` (`0x01`): **Candidate Fresh** - Candidate frame received within freshness window (250 ms).
- `Bit 1` (`0x02`): **Calibration Verified** - Physical tachometer validation completed (Reserved: always `0` in v1).
- `Bit 2` (`0x04`): **RPM Mapped** - Vehicle profile RPM arbitration ID mapped (`0` in CAN_CAPTURE mode).
- `Bit 3` (`0x08`): **Shift Lights Enabled** - Physical shift lights active (`0` in CAN_CAPTURE mode).
- `Bit 4` (`0x10`): **CAN Healthy** - ESP-IDF TWAI controller is operational (not in bus-off/halted).
- `Bit 5` (`0x20`): **Recent CAN Traffic** - At least one frame received in the last 1000 ms.
- `Bit 6` (`0x40`): **Flash Recording Active** - Actively buffering/writing frames to LittleFS.
- `Bit 7` (`0x80`): **Capture Preserved** - Previous session capture is preserved on flash.

---

### 3.2 System Health Packet (`0xd212`)

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
- `Bit 1` (`0x02`): **Storage Full** - Flash capture reached 1,000,000 byte quota or storage full.
- `Bit 2` (`0x04`): **RX Degraded** - Hardware or driver dropped frames (`missed > 0` or `overruns > 0`).
- `Bit 3` (`0x08`): **Client Connected** - A BLE central client is connected.
- `Bits 4..7`: Reserved (set to 0).

---

### 3.3 Diagnostic Log Chunk Packet (`0xd213`)

Used for streaming both live diagnostic events and paginated historical log entries (`/diagnostic.log`).

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `flags` | `uint8_t` | Chunk control flags (see Section 3.3.1) |
| 2..3 | `messageId` | `uint16_t` | Unique message identifier for this log line |
| 4 | `chunkIndex` | `uint8_t` | 0-based index of this chunk within the message |
| 5 | `payloadLength` | `uint8_t` | Number of valid payload bytes in `payload` (1..14) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Raw ASCII log text chunk, or End-of-Page metadata |

#### 3.3.1 Log Chunk Flags (`flags`)
- `Bit 0` (`0x01`): **First Chunk** - First segment of a message (`chunkIndex == 0`).
- `Bit 1` (`0x02`): **Final Chunk** - Final segment of this message (completes assembly).
- `Bit 2` (`0x04`): **End-of-Page** - Signifies completion of a requested historical page.
  - When `BLE_LOG_FLAG_END_OF_PAGE` is set:
    - `payload[0..3]` = `uint32_t` next byte offset in flash log file (little-endian).
    - `payload[4]` = `uint8_t` boolean (`1` if EOF reached, `0` if more pages available).
- `Bit 3` (`0x08`): **Error Marker** - Signifies an internal error reading storage.

---

### 3.4 Control Request Packet (`0xd214`)

Sent by the Android app via GATT `Write With Response`.

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `opcode` | `uint8_t` | Command identifier (see Section 3.4.1) |
| 2..N | `payload` | `uint8_t[]` | Command arguments |

#### 3.4.1 Supported Control Commands
- **`0x10` (`BLE_CTRL_CMD_GET_LOG_PAGE`)**:
  - Length: Exactly 6 bytes: `[0x01, 0x10, offset_u32_LE]`
  - Requests up to 20 lines of diagnostic logs starting at byte offset in LittleFS `/diagnostic.log`.
  - When all lines of the page are transmitted, an End-of-Page packet is delivered with the next offset.
- **`0x11` (`BLE_CTRL_CMD_CANCEL_LOGS`)**:
  - Length: Exactly 2 bytes: `[0x01, 0x11]`
  - Cancels an in-progress historical log page retrieval immediately.

---

## 4. Concrete Binary Fixtures and Test Vectors

### Fixture A: Key-On / Idle Live RPM (ID 0x301, Raw=44, Mult=40.0 -> 1760 RPM)
- `version`: `0x01`
- `statusFlags`: `0x71` (Bit 0 Fresh + Bit 4 Healthy + Bit 5 Traffic + Bit 6 Recording Active)
- `sequenceNumber`: `0x002A` (42)
- `uptimeMs`: `12345` (`0x00003039`)
- `provisionalRpm`: `1760` (`0x06E0`)
- `candidateRawByte`: `44` (`0x2C`)
- `captureStage`: `2` (EngineIdling)
- `canFramesRx`: `1500` (`0x000005DC`)
- `candidateAgeMs`: `20` (`0x0014`)
- `reserved`: `0x0000`

**Hex Bytes (20 bytes):**
```text
01 71 2A 00 39 30 00 00 E0 06 2C 02 DC 05 00 00 14 00 00 00
```

### Fixture B: No CAN Traffic (Waiting State)
- `version`: `0x01`
- `statusFlags`: `0x90` (Bit 4 Healthy + Bit 7 Capture Preserved)
- `sequenceNumber`: `0x0001` (1)
- `uptimeMs`: `5000` (`0x00001388`)
- `provisionalRpm`: `65535` (`0xFFFF` - Unavailable)
- `candidateRawByte`: `0` (`0x00`)
- `captureStage`: `1` (KeyOn)
- `canFramesRx`: `0` (`0x00000000`)
- `candidateAgeMs`: `65535` (`0xFFFF` - Never received)
- `reserved`: `0x0000`

**Hex Bytes (20 bytes):**
```text
01 90 01 00 88 13 00 00 FF FF 00 01 00 00 00 00 FF FF 00 00
```

### Fixture C: Health Packet (Normal Operation)
- `version`: `0x01`
- `healthFlags`: `0x09` (Bit 0 Storage Mounted + Bit 3 Client Connected)
- `sequenceNumber`: `0x000A` (10)
- `missedFrames`: `0` (`0x00000000`)
- `overrunFrames`: `0` (`0x00000000`)
- `busErrors`: `0` (`0x00000000`)
- `recordedFrames`: `23370` (`0x00005B4A`)

**Hex Bytes (20 bytes):**
```text
01 09 0A 00 00 00 00 00 00 00 00 00 00 00 00 00 4A 5B 00 00
```

### Fixture D: Control Request GET_LOG_PAGE (Offset = 4096)
- `version`: `0x01`
- `opcode`: `0x10` (`BLE_CTRL_CMD_GET_LOG_PAGE`)
- `offset`: `4096` (`0x00001000` little-endian)

**Hex Bytes (6 bytes):**
```text
01 10 00 10 00 00
```
