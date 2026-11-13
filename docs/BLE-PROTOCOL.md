# PulseShift BLE Protocol Specification (v1 & v2)

**Document Version:** 2.1.0  
**Target Hardware:** ESP32 DevKit V1 (ESP32-D0WD-V3)  
**Companion App:** PulseShift Android App (`D:\Development\PulseShift\android`)  
**Firmware Library:** NimBLE-Arduino 2.3.8  
**Arduino-ESP32 Core:** 3.3.12 (ESP-IDF 5.3+)

---

## 1. Overview and Architecture

PulseShift BLE Protocol defines the Bluetooth Low Energy GATT communication between the PulseShift ESP32 motorcycle firmware and the companion Android smartphone application.

### Key Tenets
1. **Unidirectional CAN Safety:** The ESP32 is a passive, receive-only automotive monitor (SN65HVD230 CTX tied to 3V3, GPIO17 air-gapped, listen-only TWAI mode). BLE configuration changes modify only local LED rendering, RPM thresholds, shift alerts, animations, and candidate monitoring parameters. BLE **never** injects frames into the vehicle CAN bus or executes destructive storage operations.
2. **Fixed 20-Byte Wire MTU:** Every characteristic command, response, and telemetry notification is constrained to exactly 20 bytes. No packet exceeds the universal BLE 4.0 ATT MTU limit (23 bytes total - 3 bytes ATT header = 20 bytes payload). No MTU renegotiation or L2CAP segmentation is required.
3. **Little-Endian Architecture:** All multi-byte integers (`uint16_t`, `uint32_t`, `float`) are serialized in standard IEEE 754 little-endian byte ordering.
4. **Resilient and Decoupled:** The ESP32 firmware functions autonomously without an active BLE connection. Telemetry notifications are sent only to subscribed GATT clients. Disconnections automatically re-arm advertising without interrupting CAN reception.
5. **NVS Persistence and Staged Transactions:** Changes can be applied immediately, staged across atomic multi-parameter transactions, and saved to non-volatile storage (NVS) using CRC32 checksums.
6. **Defensible Cryptographic Owner Security:** Physical presence proof (60-second authorization window triggered by the physical BOOT button on the ESP32) is mandatory for initial owner enrollment. Once enrolled, the phone authenticates via cryptographic challenge-response (SHA-256) without requiring physical button presses for ordinary configuration adjustments. Arbitrary tokens are strictly rejected.

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
- `Bit 0` (`0x01`): `BLE_RPM_FLAG_CANDIDATE_FRESH` (Candidate frame received within timeout)
- `Bit 1` (`0x02`): `BLE_RPM_FLAG_CALIBRATION_VERIFIED` (Calibration verified by rider/developer)
- `Bit 2` (`0x04`): `BLE_RPM_FLAG_RPM_MAPPED` (Candidate mapped to production shift lights)
- `Bit 3` (`0x08`): `BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED` (Shift lights actively driving LEDs)
- `Bit 4` (`0x10`): `BLE_RPM_FLAG_CAN_HEALTHY` (TWAI driver in running state)
- `Bit 5` (`0x20`): `BLE_RPM_FLAG_TRAFFIC_ACTIVE` (CAN frames received in last 1000 ms)
- `Bit 6` (`0x40`): `BLE_RPM_FLAG_RECORDING_ACTIVE` (Flash binary capture running)
- `Bit 7` (`0x80`): `BLE_RPM_FLAG_CAPTURE_PRESERVED` (Prior flash recording preserved)

---

### 3.2 System Health Packet (`0xd212`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `healthFlags` | `uint8_t` | System health bitmask (Bit 0=CanInit, Bit 1=StorageMounted, Bit 2=StorageFull, Bit 3=Preserved, Bit 4=Recording, Bit 5=BusError) |
| 2..3 | `freeHeapKb` | `uint16_t` | Free heap memory in KiB |
| 4..7 | `uptimeSeconds` | `uint32_t` | Controller uptime in integer seconds |
| 8..11 | `canRxFrames` | `uint32_t` | Total CAN frames received since boot |
| 12..13 | `canRxMissed` | `uint16_t` | TWAI RX buffer missed frames |
| 14..15 | `canRxFifoOverruns` | `uint16_t` | Software queue overruns |
| 16..17 | `canBusErrors` | `uint16_t` | TWAI bus error count |
| 18..19 | `recordedFramesSession`| `uint16_t` | CAN frames saved to storage in current session |

---

### 3.3 Diagnostic Log Chunk Packet (`0xd213`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `flags` | `uint8_t` | Bit 0: `FIRST_CHUNK` (`0x01`), Bit 1: `FINAL_CHUNK` (`0x02`), Bit 2: `END_OF_PAGE` (`0x04`) |
| 2..3 | `messageId` | `uint16_t` | Monotonic log message identifier |
| 4 | `chunkIndex` | `uint8_t` | 0-based chunk index within message |
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
| 11 | `securityMode` | `uint8_t` | Security mode: `3` = Hybrid (Physical BOOT window presence OR Challenge-Response) |
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
| 11..19 | `payload[9]` | `uint8_t[9]` | Optional response data (e.g. 4-byte challenge nonce) |

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
| `0x20` | `SAVE_CONFIG` | None | Commit active configuration to flash NVS (`"ps_cfg"`) |
| `0x21` | `RESTORE_DEFAULTS` | `[0xA5, 0x5A]` | Factory reset configuration and erase owner credentials |
| `0x24` | `ENROLL_OWNER` | `[secret: u8[12]]` | One-time owner enrollment (requires open BOOT window) |
| `0x25` | `AUTHENTICATE` | `[proof: u8[8]]` | Authenticate connection session via SHA-256 proof |
| `0x26` | `REQUEST_CHALLENGE`| None | Request 4-byte single-use cryptographic challenge nonce |
| `0x30` | `BEGIN_TRANSACTION`| None | Start staged transaction (30s timeout) |
| `0x31` | `COMMIT_TRANSACTION`| None | Validate all staged changes and apply atomically |
| `0x32` | `ABORT_TRANSACTION` | None | Discard all staged changes without modifying active config |

---

### 4.5 Status Codes

| Code | Name | Description |
|---|---|---|
| `0` | `BLE_CFG_STATUS_OK` | Command executed successfully |
| `1` | `BLE_CFG_STATUS_INVALID_PARAM` | Parameter out of range or non-monotonic |
| `2` | `BLE_CFG_STATUS_UNSUPPORTED` | Opcode or parameter unsupported |
| `3` | `BLE_CFG_STATUS_BUSY` | Controller busy or under rate-limiting lockout |
| `4` | `BLE_CFG_STATUS_UNAUTHORIZED` | Authorization required; enrollment or authentication needed |
| `5` | `BLE_CFG_STATUS_INVALID_STATE` | Staged outside tx, expired challenge, or un-enrolled challenge request |
| `6` | `BLE_CFG_STATUS_STORAGE_ERROR` | Flash NVS write failed |
| `7` | `BLE_CFG_STATUS_VERSION_MISMATCH`| Protocol version mismatch |
| `8` | `BLE_CFG_STATUS_VALIDATION_FAILED`| Configuration validation failed |

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

## 5. Owner Enrollment and Challenge-Response Authentication

The PulseShift hardware is a dedicated headless embedded unit without a screen or keyboard. To defend against unauthorized vehicle tuning or drive-by configuration tampering, PulseShift enforces defensible physical owner presence and cryptographic authentication:

```text
[Phone Application]                                        [PulseShift ESP32]
         |                                                          |
         |  (User presses physical BOOT button on ESP32)            |
         |  <-------------------------------------------------------| BOOT Window Open (60s)
         |                                                          | (LEDs pulse Cyan)
         |  ENROLL_OWNER (0x24) [secret: 12 bytes]                  |
         |--------------------------------------------------------->| Store in NVS ("ps_sec")
         |  Result (0x24, OK, PERSISTED | APPLIED)                  | Session Authorized
         |<---------------------------------------------------------|
         :                                                          :
         :       (Normal Phone Use in subsequent sessions)          :
         |                                                          |
         |  REQUEST_CHALLENGE (0x26)                                |
         |--------------------------------------------------------->| Generate 32-bit nonce
         |  Result (0x26, OK, payload: challenge [4 bytes LE])      | 30-second TTL
         |<---------------------------------------------------------|
         |                                                          |
         |  Compute Proof:                                          |
         |  SHA-256(secret[12] || challenge[4] || seq[2])[:8]       |
         |                                                          |
         |  AUTHENTICATE (0x25) [proof: 8 bytes]                    | Single-use nonce consumed
         |--------------------------------------------------------->| Constant-time comparison
         |  Result (0x25, OK)                                       | Session Authorized (5 min)
         |<---------------------------------------------------------|
         |                                                          |
         |  SET_BRIGHTNESS, SET_EFFECT, etc.                        | Writes Permitted
         |--------------------------------------------------------->|
```

### 5.1 Owner Enrollment Flow
1. The user presses the physical **BOOT** button (GPIO0) on the ESP32.
2. The controller opens a **60-second authorization window** and pulses the LED strip in cyan.
3. The phone sends opcode `0x24` (`BLE_CFG_OP_ENROLL_OWNER`) containing a randomly generated 12-byte cryptographic secret.
4. The ESP32 validates the open BOOT window, verifies the secret is non-trivial, and stores it in encrypted/dedicated NVS namespace `"ps_sec"`.
5. The session is immediately marked authorized, allowing immediate configuration changes.

### 5.2 Subsequent Authenticated Phone Sessions
1. In future rides, the phone connects via BLE without pressing the BOOT button.
2. The phone sends opcode `0x26` (`BLE_CFG_OP_REQUEST_CHALLENGE`).
3. The ESP32 returns a 4-byte pseudo-random challenge nonce (`esp_random()`) with a 30-second time-to-live (TTL).
4. The phone computes the 8-byte authentication proof:
   $$\text{Proof} = \text{SHA-256}(\text{ownerSecret}[12] \mathbin{\Vert} \text{challenge}[4 \text{ LE}] \mathbin{\Vert} \text{sequence}[2 \text{ LE}])[\text{first 8 bytes}]$$
5. The phone sends opcode `0x25` (`BLE_CFG_OP_AUTHENTICATE`) with the 8-byte proof.
6. The ESP32 consumes the challenge immediately (preventing replay attacks), computes the matching SHA-256 proof, and verifies in constant time.
7. Upon verification, the session is authorized for 5 minutes of continuous activity (refreshed on each command).

### 5.3 Negative Security Defenses
- **Arbitrary Token Rejection:** Opcode `0x25` with arbitrary tokens is rejected outside the physical BOOT window (`BLE_CFG_STATUS_UNAUTHORIZED = 4`).
- **Replay Attack Defense:** Every challenge is strictly single-use. Re-submitting the same proof without requesting a fresh challenge fails with `BLE_CFG_STATUS_INVALID_STATE = 5`.
- **Challenge Expiration:** Challenges expire after 30 seconds (`BLE_CFG_STATUS_INVALID_STATE = 5`).
- **Brute-Force Rate Limiting:** After 5 consecutive failed authentications, challenge requests are locked out for 3 seconds (`BLE_CFG_STATUS_BUSY = 3`).
- **Disconnect Invalidation:** Client disconnection immediately terminates the authenticated session and invalidates pending challenges.
- **Factory Reset Wiping:** Executing `RESTORE_DEFAULTS` (`0x21`) with magic bytes `[0xA5, 0x5A]` resets all parameters and erases the owner credential from `"ps_sec"`.

---

## 6. Cross-Language Byte Test Vectors

The following deterministic test vector is published to verify cross-language byte serialization between Python (`test_ble_config_v2.py`), Android Kotlin (`PulseShiftBleProtocolTest.kt`), and C++ ESP32 firmware:

### 6.1 Challenge-Response Vector
```text
Owner Secret (12 bytes ASCII):
"PULSE_KEY_96" -> [0x50, 0x55, 0x4c, 0x53, 0x45, 0x5f, 0x4b, 0x45, 0x59, 0x5f, 0x39, 0x36]

Challenge Nonce (uint32_t LE = 0x12345678):
[0x78, 0x56, 0x34, 0x12]

Request Sequence (uint16_t LE = 42):
[0x2a, 0x00]

Combined Hash Input (18 bytes):
50 55 4c 53 45 5f 4b 45 59 5f 39 36 78 56 34 12 2a 00

SHA-256 Full Digest (32 bytes):
12 38 a0 9b 10 7e 9d 79 38 a6 37 da 41 e9 c4 ee fa ec eb c4 e0 e0 0e 86 5d dd d1 fa 1c 35 c4 cc

Expected Auth Proof (First 8 bytes):
[0x12, 0x38, 0xa0, 0x9b, 0x10, 0x7e, 0x9d, 0x79] -> Hex string: "1238a09b107e9d79"
```
