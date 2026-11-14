# PulseShift BLE Protocol Specification (v1 & v2)

**Document Version:** 2.2.0
**Target Hardware:** ESP32 DevKit V1 (ESP32-D0WD-V3)
**Companion App:** PulseShift Android App (`D:\Development\PulseShift\android`)
**Firmware Library:** NimBLE-Arduino 2.3.8
**Arduino-ESP32 Core:** 3.3.12 (ESP-IDF 5.3+)

---

## 1. Overview and Architecture

PulseShift BLE Protocol defines the Bluetooth Low Energy GATT communication between the PulseShift ESP32 motorcycle firmware and the companion Android smartphone application. The Android app is the complete command interface: every normal configuration and diagnostic operation is executable over BLE, and the firmware runs fully standalone without USB, serial or a PC.

### Key Tenets
1. **Unidirectional CAN Safety:** The ESP32 is a passive, receive-only automotive monitor (SN65HVD230 CTX tied to 3V3, GPIO17 air-gapped, listen-only TWAI mode with a zero-length TX queue). BLE configuration changes modify only local LED rendering, RPM thresholds, shift alerts, animations, and candidate monitoring parameters. BLE **never** injects frames into the vehicle CAN bus, and the firmware contains no CAN transmit API at all.
2. **Fixed 20-Byte Wire MTU:** Every characteristic command, response, and telemetry notification is constrained to exactly 20 bytes. No MTU renegotiation or L2CAP segmentation is required; large payloads are reassembled at the application layer.
3. **Little-Endian Architecture:** All multi-byte integers (`uint16_t`, `uint32_t`, `float`) are serialized in standard IEEE 754 little-endian byte ordering.
4. **Resilient and Decoupled:** The ESP32 firmware functions autonomously without an active BLE connection. Telemetry notifications are sent only to subscribed GATT clients. Disconnections automatically re-arm advertising without interrupting CAN reception or LED rendering.
5. **NVS Persistence and Staged Transactions:** Changes can be applied immediately, staged across atomic multi-parameter transactions, and saved to non-volatile storage (NVS) using CRC32 checksums.
6. **Defensible Cryptographic Owner Security:** Physical presence proof (60-second authorization window triggered by the physical BOOT button on the ESP32) is mandatory for owner enrollment and credential recovery. Once enrolled, the phone authenticates via cryptographic challenge-response (SHA-256) without requiring physical button presses for ordinary configuration adjustments.
7. **Acknowledged Execution:** A Bluetooth write returning success is NOT proof of execution. Every v2 command carries a client `sequence` echoed in the result packet together with a status code and result flags; the app requires request/response correlation plus snapshot readback for configuration changes.

---

## 2. GATT Service and Characteristics

**Advertised Device Name:** `PulseShift`
**Primary Service UUID:** `fe12d210-3d4f-4b9d-a21c-92782ecb4000`

| Purpose | Full UUID | Properties | Frequency / Access | Description |
|---|---|---|---|---|
| **RPM Telemetry** | `fe12d211-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 10 Hz (100 ms) | Live engine speed, raw candidate byte, age, CAN frame count |
| **System Health** | `fe12d212-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | 1 Hz (1000 ms) | CAN bus health, missed/overrun counters, storage status |
| **Diagnostic Logs** | `fe12d213-3d4f-4b9d-a21c-92782ecb4000` | `NOTIFY` | Event / Paced (20 ms) | Live diagnostic events, paginated historical logs, CAN capture pages |
| **Control Requests (v1)**| `fe12d214-3d4f-4b9d-a21c-92782ecb4000` | `WRITE` | On-demand | GET_LOG_PAGE / CANCEL_LOGS / GET_CAPTURE_PAGE / CANCEL_CAPTURE |
| **Config Command (v2)** | `fe12d215-3d4f-4b9d-a21c-92782ecb4000` | `WRITE` | On-demand | Configuration write/read requests, device control, transaction controls |
| **Config Result (v2)** | `fe12d216-3d4f-4b9d-a21c-92782ecb4000` | `READ`, `NOTIFY` | Immediate response | Status code, active revision counter, and result flags |
| **Config Snapshot (v2)**| `fe12d217-3d4f-4b9d-a21c-92782ecb4000` | `NOTIFY` | Paced (20 ms stream) | Multi-chunk serialized configuration TLV blob (schema 3) or diagnostics TLV blob (schema 1) with CRC32 |
| **Capabilities (v2)** | `fe12d218-3d4f-4b9d-a21c-92782ecb4000` | `READ` | Static / On revision | Hardware capabilities, feature mask, firmware version |

---

## 3. Protocol v1 Telemetry Packets

### 3.1 RPM Telemetry Packet (`0xd211`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `statusFlags` | `uint8_t` | Bitmask representing live subsystem states (see Section 3.1.1) |
| 2..3 | `sequenceNumber` | `uint16_t` | Incremental rolling packet sequence counter (0..65535) |
| 4..7 | `uptimeMs` | `uint32_t` | ESP32 monotonic uptime in milliseconds (`millis()`) |
| 8..9 | `provisionalRpm` | `uint16_t` | Estimated RPM from the unified decoder. `65535` = unavailable/stale |
| 10 | `candidateRawByte` | `uint8_t` | Low byte of the raw candidate value (valid only if fresh flag set) |
| 11 | `captureStage` | `uint8_t` | Current field stage: `0`=Standby, `1`=KeyOn, `2`=Idling, `3`=GentleRPM |
| 12..15 | `canFramesRx` | `uint32_t` | Cumulative count of total CAN frames received by TWAI driver |
| 16..17 | `candidateAgeMs` | `uint16_t` | Milliseconds elapsed since last candidate frame. `65535` = never received |
| 18..19 | `reserved` | `uint16_t` | Reserved for alignment; always zero |

#### 3.1.1 Status Flags Bitmask (`statusFlags`)
- `Bit 0` (`0x01`): `BLE_RPM_FLAG_CANDIDATE_FRESH` (Candidate frame received within the freshness timeout, default 250 ms)
- `Bit 1` (`0x02`): `BLE_RPM_FLAG_CALIBRATION_VERIFIED` (Verification record bound to the current decoder fingerprint)
- `Bit 2` (`0x04`): `BLE_RPM_FLAG_RPM_MAPPED` (The unified candidate decoder is the production RPM mapping; always set on 2.2.0+)
- `Bit 3` (`0x08`): `BLE_RPM_FLAG_SHIFT_LIGHTS_ENABLED` (Set only while RPM-driven lighting is actually rendering from live CAN data: the verified bar (effect 4), the unverified provisional preview (effect 5) or their warning overlay. Clear during decorative effects 0-3, safeguard blanks and the synthetic bench test)
- `Bit 4` (`0x10`): `BLE_RPM_FLAG_CAN_HEALTHY` (TWAI driver in running state)
- `Bit 5` (`0x20`): `BLE_RPM_FLAG_TRAFFIC_ACTIVE` (CAN frames received in last 1000 ms)
- `Bit 6` (`0x40`): `BLE_RPM_FLAG_RECORDING_ACTIVE` (Flash binary capture running)
- `Bit 7` (`0x80`): `BLE_RPM_FLAG_CAPTURE_PRESERVED` (Prior flash recording preserved)

When the candidate goes stale, `provisionalRpm` reports `65535` and LEDs blank: the device never continues rendering the last received RPM indefinitely.

---

### 3.2 System Health Packet (`0xd212`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `healthFlags` | `uint8_t` | Bit 0: Storage mounted (`0x01`), Bit 1: Storage full (`0x02`), Bit 2: RX degraded (`0x04`), Bit 3: BLE client connected (`0x08`) |
| 2..3 | `sequenceNumber` | `uint16_t` | Incremental rolling packet sequence counter |
| 4..7 | `missedFrames` | `uint32_t` | TWAI driver-missed CAN frames in current session |
| 8..11 | `overrunFrames` | `uint32_t` | Hardware FIFO overruns in current session |
| 12..15 | `busErrors` | `uint32_t` | TWAI bus error counter |
| 16..19 | `recordedFrames` | `uint32_t` | CAN frames recorded to flash in current session |

---

### 3.3 Diagnostic Log Chunk Packet (`0xd213`, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `1` |
| 1 | `flags` | `uint8_t` | Bit 0: `FIRST_CHUNK` (`0x01`), Bit 1: `FINAL_CHUNK` (`0x02`), Bit 2: `END_OF_PAGE` (`0x04`), Bit 3: `ERROR` (`0x08`), Bit 4: `CAPTURE_LINE` (`0x10`) |
| 2..3 | `messageId` | `uint16_t` | Monotonic log message identifier |
| 4 | `chunkIndex` | `uint8_t` | 0-based chunk index within message |
| 5 | `payloadLength` | `uint8_t` | Number of valid payload bytes in `payload` (1..14) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Raw ASCII log text chunk, or End-of-Page metadata |

For End-of-Page packets: `payloadLength = 5`, `payload[0..3]` = next byte offset (diagnostic logs) or next record index (capture pages) as `uint32_t` LE, `payload[4]` = finished flag (`1` = no more data).

Capture pages stream CSV-like lines flagged with `CAPTURE_LINE`:
```text
CAP,<timestampMs>,<stage>,<idHex>,<ext>,<dlc>,<dataHex>
```
These lines are reassembled as multi-chunk messages and routed to the capture flow (never mixed with diagnostic messages).

---

### 3.4 Control Requests (`0xd214`, WRITE)

**Authorization (2.2.1+):** `GET_LOG_PAGE` and `GET_CAPTURE_PAGE` return vehicle
telemetry (raw CAN capture records and diagnostic history), so they require an
authenticated owner session bound to the requesting connection, exactly like
mutating configuration commands. An unauthenticated request is rejected, logged
to the device diagnostic event log and answered with a single
`BLE_LOG_FLAG_ERROR` chunk ("AUTH REQUIRED...") on the log characteristic.
`CANCEL_LOGS` and `CANCEL_CAPTURE` stay unauthenticated: they only stop a
transfer and disclose nothing.

| Opcode | Name | Frame | Description |
|---|---|---|---|
| `0x10` | `GET_LOG_PAGE` | `[0x01, 0x10, offset: u32 LE]` (6 bytes) | **Auth required.** Stream one 20-line historical diagnostic log page from `offset` |
| `0x11` | `CANCEL_LOGS` | `[0x01, 0x11]` (2 bytes) | Cancel the diagnostic log page transfer |
| `0x12` | `GET_CAPTURE_PAGE` | `[0x01, 0x12, startRecord: u32 LE]` (6 bytes) | **Auth required.** Stream one bounded page (12 records) of the recorded CAN capture from `startRecord`. Reading never mutates the capture file |
| `0x13` | `CANCEL_CAPTURE` | `[0x01, 0x13]` (2 bytes) | Cancel the capture page transfer |

---

## 4. Protocol v2 Configuration Architecture

### 4.1 Device Capabilities (`0xd218`, READ, 20 Bytes)

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1 | `physicalLedCount`| `uint8_t` | Physical LED count = `6` |
| 2..5 | `featureMask` | `uint32_t` | Feature bitmask (see below; firmware 2.2.0 = `0x7FFF`) |
| 6..9 | `currentRevision` | `uint32_t` | Monotonic active configuration revision counter |
| 10 | `presetCapacity` | `uint8_t` | Hardware preset slots (`0` = smartphone-managed presets) |
| 11 | `securityMode` | `uint8_t` | Security mode: `3` = Hybrid (Physical BOOT window presence OR Challenge-Response) |
| 12 | `firmwareMajor` | `uint8_t` | Firmware version major = `2` |
| 13 | `firmwareMinor` | `uint8_t` | Firmware version minor = `2` |
| 14 | `firmwarePatch` | `uint8_t` | Firmware version patch = `0` |
| 15..19 | `reserved[5]` | `uint8_t[5]` | Reserved for future expansion; all zeros |

#### Feature Mask Bits (`featureMask`)
- `Bit 0` (`0x0001`): `BLE_FEAT_INDIVIDUAL_RGB`
- `Bit 1` (`0x0002`): `BLE_FEAT_EFFECTS`
- `Bit 2` (`0x0004`): `BLE_FEAT_STARTUP_ANIM`
- `Bit 3` (`0x0008`): `BLE_FEAT_RPM_THRESHOLDS`
- `Bit 4` (`0x0010`): `BLE_FEAT_SHIFT_WARNING`
- `Bit 5` (`0x0020`): `BLE_FEAT_RPM_MULTIPLIER`
- `Bit 6` (`0x0040`): `BLE_FEAT_CANDIDATE_SOURCE`
- `Bit 7` (`0x0080`): `BLE_FEAT_PHONE_PRESETS`
- `Bit 8` (`0x0100`): `BLE_FEAT_NVS_PERSISTENCE`
- `Bit 9` (`0x0200`): `BLE_FEAT_AUTH_WRITES`
- `Bit 10` (`0x0400`): `BLE_FEAT_DECODER_EXT` (width/endianness/offset/clamp)
- `Bit 11` (`0x0800`): `BLE_FEAT_HYSTERESIS`
- `Bit 12` (`0x1000`): `BLE_FEAT_CAL_WORKFLOW`
- `Bit 13` (`0x2000`): `BLE_FEAT_DIAGNOSTICS_TLV`
- `Bit 14` (`0x4000`): `BLE_FEAT_CAPTURE_EXPORT`
- `Bit 15` (`0x8000`): `BLE_FEAT_CAPTURE_STAGE` (authenticated stage-label command)
- `Bit 16` (`0x10000`): `BLE_FEAT_LED_BENCH_TEST` (authenticated 10-second synthetic test)

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

Incoming commands are queued through a bounded SPSC ring (4 entries); overflowed commands are counted and dropped rather than corrupting the queue.

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
| 11..19 | `payload[9]` | `uint8_t[9]` | Optional response data (challenge nonce, calibration fit, statuses) |

---

### 4.4 Command Opcodes

**Authorization:** read-only queries (`0x01`, `0x02`, `0x43`, `0x46`, `0x50`) and the auth flow itself (`0x24`, `0x25`, `0x26`) are unauthenticated. Every other opcode requires a session bound to the current connection (challenge-response) or an open physical BOOT window.

| Opcode | Name | Payload Arguments | Description |
|---|---|---|---|
| `0x01` | `GET_CAPABILITIES` | None | Updates Capabilities characteristic |
| `0x02` | `GET_SNAPSHOT` | None | Triggers complete config snapshot streaming on `0xd217` |
| `0x10` | `SET_POWER` | `[power: u8 (0 or 1)]` | Master LED strip power switch |
| `0x11` | `SET_BRIGHTNESS` | `[brightness: u8 (0..255)]` | Master LED brightness |
| `0x12` | `SET_GLOBAL_RGB`| `[R: u8, G: u8, B: u8]` | Set uniform color across all 6 LEDs |
| `0x13` | `SET_LED_RGB` | `[index: u8 (0..5), R, G, B]` | Set individual LED color |
| `0x14` | `SET_EFFECT` | `[effectId: u8 (0..5), speed: u8 (0..100)]` | 0=Static, 1=Breathing, 2=Rainbow, 3=Chase, 4=Verified RPM Bar, **5=Provisional CAN RPM Preview (unverified)** |
| `0x15` | `SET_STARTUP` | `[animId: u8 (0..5), durationMs: u16 LE]` | 0=None, 1=Sweep, 2=Wave, 3=Pulse, 4=CenterOut, 5=Strobe Rush |
| `0x16` | `SET_LED_THRESHOLD`| `[index: u8 (0..5), rpm: u16 LE]` | Set threshold for LED index (must remain monotonic) |
| `0x17` | `SET_SHIFT_WARNING`| `[warningRpm: u16 LE, patternId: u8]` | Pattern: 0=Flash (10 Hz), 1=Strobe (20 Hz), 2=Solid, 3=Off |
| `0x18` | `SET_RPM_MULTIPLIER`| `[multX1000: u32 LE]` | Provisional multiplier (0.1..1000.0). **Invalidates calibration!** |
| `0x19` | `SET_CANDIDATE_SOURCE` | `[canId: u32 LE, byteIdx: u8, ext: u8]` | Target CAN candidate source. **Invalidates calibration!** |
| `0x1A` | `SET_LED_DIRECTION`| `[direction: u8 (0=Forward, 1=Reverse)]`| Shift light display direction |
| `0x1B` | `SET_WARNING_RGB`| `[R: u8, G: u8, B: u8]` | Warning color for over-rev alert |
| `0x1C` | `SET_RPM_HYSTERESIS` | `[hysteresisRpm: u16 LE (0..2000)]` | Falling-RPM dead band for bar/warning latching |
| `0x1D` | `SET_RPM_DECODER_EXT` | `[width: u8 (1|2), endian: u8, offsetX1000: s32 LE, maxValid: u16 LE]` (8 bytes) | Byte width/order, RPM offset, validity clamp. **Invalidates calibration!** |
| `0x20` | `SAVE_CONFIG` | None | Commit active configuration to flash NVS (`"ps_cfg"`) |
| `0x21` | `RESTORE_DEFAULTS` | `[0xA5, 0x5A]` | Restore safe lighting/decoder defaults and save to NVS. **Owner credentials and captures are preserved.** |
| `0x22` | `RELOAD_CONFIG` | None | Re-read the persisted NVS blob and apply it to the active state |
| `0x24` | `ENROLL_OWNER` | `[secret: u8[12]]` | Owner enrollment or credential recovery (requires open BOOT window); safe re-enrollment overwrites the old credential |
| `0x25` | `AUTHENTICATE` | `[proof: u8[8]]` | Authenticate connection session via SHA-256 proof |
| `0x26` | `REQUEST_CHALLENGE`| None | Request 4-byte single-use cryptographic challenge nonce |
| `0x30` | `BEGIN_TRANSACTION`| None | Start staged transaction (30s timeout) |
| `0x31` | `COMMIT_TRANSACTION`| None | Validate all staged changes and apply atomically |
| `0x32` | `ABORT_TRANSACTION` | None | Discard all staged changes without modifying active config |
| `0x40` | `CAL_BEGIN` | None | Start the mobile-assisted calibration session (10 min timeout) |
| `0x41` | `CAL_ABORT` | None | Abort the calibration session |
| `0x42` | `CAL_CAPTURE_START` | None | Open the 2 s raw sampling window at the current operating point |
| `0x43` | `CAL_CAPTURE_STATUS` | None | Query window. Result payload: `[samples: u16 LE, elapsedMs: u16 LE]` |
| `0x44` | `CAL_POINT_COMMIT` | `[refRpm: u16 LE]` | Close the window, require >=10 stable samples (stddev <= 1.5 raw), store the point |
| `0x45` | `CAL_COMPUTE` | `[apply: u8]` | Fit scale/offset over 3..6 points, validate evidence. Result payload on OK (9 bytes): `[multiplierX1000: s32 LE, offsetX1000: s32 LE, info: u8]` where info = pointsUsed (low nibble) \| worst residual in 10-RPM steps clamped to 15 (high nibble); on failure: reason string (<= 9 bytes). Applying binds the verification record to the decoder fingerprint |
| `0x46` | `CAL_STATUS` | None | Query session. Result payload: `[active: u8, points: u8, captureActive: u8, samples: u16 LE]` |
| `0x50` | `GET_DIAGNOSTICS` | None | Stream the diagnostics TLV blob on `0xd217` (Section 4.8) |
| `0x51` | `RESTART_DEVICE` | `[0xA5, 0x5A]` | Acknowledge, then soft-restart the ESP32 |
| `0x52` | `CLEAR_DIAGNOSTICS` | `[0xA5, 0x5A]` | Clear the diagnostic event log ONLY (captures and credentials untouched) |
| `0x53` | `SET_CAPTURE_RECORDING` | `[enable: u8]` | Start/stop flash recording; start is state-gated (damaged/preserved/full storage => `INVALID_STATE`) |
| `0x54` | `SET_CAPTURE_STAGE` | `[stage: u8 (1..3)]` | Label subsequent CAN capture records: 1=KeyOn, 2=Idle, 3=RPM sweep. Does not change operation or synthesize CAN data. |
| `0x55` | `SET_LED_TEST_RPM` | `[testRpm: u16 LE (0..20000), enable: u8 (0 or 1)]` | Authenticated, temporary 10-second synthetic six-LED bench test. `enable=0` stops it; independent of real CAN data. Does not persist or inject CAN frames. |

---

### 4.5 Status Codes

| Code | Name | Description |
|---|---|---|
| `0` | `BLE_CFG_STATUS_OK` | Command executed successfully |
| `1` | `BLE_CFG_STATUS_INVALID_PARAM` | Parameter out of range or non-monotonic |
| `2` | `BLE_CFG_STATUS_UNSUPPORTED` | Opcode or parameter unsupported |
| `3` | `BLE_CFG_STATUS_BUSY` | Controller busy or under rate-limiting lockout |
| `4` | `BLE_CFG_STATUS_UNAUTHORIZED` | Authorization required; enrollment or authentication needed |
| `5` | `BLE_CFG_STATUS_INVALID_STATE` | Staged outside tx, expired challenge, storage not usable, or invalid session |
| `6` | `BLE_CFG_STATUS_STORAGE_ERROR` | Flash NVS write failed |
| `7` | `BLE_CFG_STATUS_VERSION_MISMATCH`| Protocol version mismatch |
| `8` | `BLE_CFG_STATUS_VALIDATION_FAILED`| Configuration validation failed |

---

### 4.6 Configuration Snapshot (`0xd217`, NOTIFY, 20-Byte Chunks)

Streamed with 20 ms pacing between chunks to prevent Bluetooth buffer congestion. The same characteristic carries diagnostics blobs (schema byte `1`) and configuration snapshots (schema byte `3`); clients dispatch on the reassembled schema byte.

| Byte Offset | Field Name | Data Type | Description |
|---|---|---|---|
| 0 | `version` | `uint8_t` | Protocol version = `2` |
| 1..2 | `snapshotId` | `uint16_t` | Snapshot identifier (little-endian; the request sequence) |
| 3 | `chunkIndex` | `uint8_t` | 0-based chunk sequence index |
| 4 | `chunkCount` | `uint8_t` | Total chunks comprising the blob |
| 5 | `payloadLength` | `uint8_t` | Number of valid bytes in this chunk (1..14) |
| 6..19 | `payload[14]` | `uint8_t[14]` | Serialized TLV fragment |

#### Reassembled Configuration Snapshot Binary Layout (schema 3)
```text
[Byte 0]     : schemaVersion (uint8_t = 3)
[Bytes 1..4] : activeRevision (uint32_t LE)
[Bytes 5..N-4]: TLV Records (see Section 4.7)
[Bytes N-3..N]: CRC32 (uint32_t LE, IEEE 802.3 over Bytes 0..N-4)
```

#### Reassembled Diagnostics Blob Binary Layout (schema 1)
```text
[Byte 0]     : schemaVersion (uint8_t = 1)
[Bytes 1..4] : revision (uint32_t LE, always 0)
[Bytes 5..N-4]: Diagnostics TLV Records (see Section 4.8)
[Bytes N-3..N]: CRC32 (uint32_t LE)
```

---

### 4.7 Configuration Snapshot TLVs (schema 3)

Every TLV record is formatted as `[Tag: uint8_t, Length: uint8_t, Value: uint8_t[Length]]`.

All 12 mandatory tags (`0x10` through `0x1B`) are always emitted; schema-3 tags `0x1C`..`0x1E` accompany them. Unknown tags must be tolerated (skipped) by clients for forward compatibility.

| Tag | Name | Length | Value Format | Description |
|---|---|---|---|---|
| `0x10` | Power | 1 | `[power: u8]` | `0` = Off, `1` = On |
| `0x11` | Brightness | 1 | `[brightness: u8]` | Master brightness `0..255` |
| `0x12` | Global Color | 3 | `[R, G, B]` | Primary single RGB color |
| `0x13` | Per-LED Colors | 18 | `[6 * (R, G, B)]` | Individual RGB colors for LEDs 0..5 |
| `0x14` | Effect & Speed | 2 | `[effectId: u8, speed: u8]` | Effect (0..5), Speed (0..100) |
| `0x15` | Startup Animation | 3 | `[animId: u8, durationMs: u16 LE]` | Startup anim (0..5), duration in ms |
| `0x16` | RPM Thresholds | 12 | `[6 * u16 LE]` | Monotonic thresholds for LEDs 0..5 |
| `0x17` | Shift Warning | 3 | `[warningRpm: u16 LE, patternId: u8]`| Warning threshold and pattern (0..3, 3 = Off) |
| `0x18` | RPM Multiplier | 4 | `[multX1000: u32 LE]` | Multiplier * 1000 (e.g. 40.0 -> 40000) |
| `0x19` | CAN Candidate | 6 | `[canId: u32 LE, byteIdx: u8, ext: u8]`| Target candidate CAN source |
| `0x1A` | LED Direction | 1 | `[direction: u8]` | `0` = Forward (0->5), `1` = Reverse (5->0) |
| `0x1B` | Warning Color | 3 | `[R, G, B]` | Alert over-rev flash color |
| `0x1C` | Decoder Extension | 8 | `[width: u8, endian: u8, offsetX1000: s32 LE, maxValid: u16 LE]` | Byte width/order, RPM offset, validity clamp |
| `0x1D` | Calibration Record | 11 | `[verified: u8, fingerprint: u32 LE, pointsUsed: u8, maxResidualRpm: u8, timestampMs: u32 LE]` | Bound verification record |
| `0x1E` | Hysteresis | 2 | `[hysteresisRpm: u16 LE]` | Threshold dead band |

---

### 4.8 Diagnostics Blob TLVs (GET_DIAGNOSTICS, schema 1)

| Tag | Name | Length | Value Format |
|---|---|---|---|
| `0x40` | Uptime | 4 | `uptimeMs: u32 LE` |
| `0x41` | Firmware Version | 3 | `[major, minor, patch]` |
| `0x42` | Reset Reason | 1 | `esp_reset_reason` of the last boot |
| `0x43` | CAN Status | 26 | `[bitrate: u32, totalRx: u32, accepted: u32, missed: u32, overrun: u32, busErrors: u32, healthy: u8, traffic: u8]` |
| `0x44` | RPM Decoder State | 34 | `[canId: u32, byteIndex: u8, byteWidth: u8, extended: u8, multX1000: s32, offsetX1000: s32, maxValid: u16, raw: u16, estimatedRpm: u16, fresh: u8, valid: u8, calVerified: u8, ageMs: u16, received: u32, rejected: u32]` |
| `0x45` | Storage State | 29 | `[total: u32, used: u32, free: u32, mounted: u8, storageFull: u8, capturePreserved: u8, damaged: u8, recording: u8, records: u32, captureSize: u32, diagLogSize: u32]` |
| `0x46` | BLE State | 5 | `[connected: u8, advertising: u8, securityMode: u8, ownerEnrolled: u8, sessionAuthorized: u8]` |
| `0x47` | Capture Info | 9 | `[stage: u8, sessionId: u32, bootCount: u32]` |
| `0x48` | Last Error | <= 96 | UTF-8 text of the last Error/Critical event this session (absent when none) |
| `0x49` | Calibration State | 13 | `[sessionActive: u8, points: u8, captureActive: u8, samples: u16, calVerified: u8, pointsUsed: u8, maxResidualRpm: u8, fingerprint: u32]` |

---

## 5. Owner Enrollment and Challenge-Response Authentication

The PulseShift hardware is a dedicated headless embedded unit. To defend against unauthorized vehicle tuning or drive-by configuration tampering, PulseShift enforces physical owner presence and cryptographic authentication:

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

### 5.1 Owner Enrollment Flow (and Credential Recovery)
1. The user presses the physical **BOOT** button (GPIO0) on the ESP32.
2. The controller opens a **60-second authorization window** and pulses the LED strip in cyan.
3. The phone sends opcode `0x24` (`ENROLL_OWNER`) containing a randomly generated 12-byte cryptographic secret.
4. The ESP32 validates the open BOOT window, verifies the secret is non-trivial, and stores it in a dedicated CRC-protected NVS namespace `"ps_sec"`.
5. The session is immediately marked authorized, bound to this connection.
6. **Credential recovery** is the same flow: pressing BOOT and re-enrolling overwrites the previous credential. Credential recovery and configuration reset are deliberately distinct operations.

### 5.2 Subsequent Authenticated Phone Sessions
1. In future rides, the phone connects via BLE without pressing the BOOT button and automatically performs the challenge-response exchange using its stored credential.
2. The phone sends opcode `0x26` (`REQUEST_CHALLENGE`).
3. The ESP32 returns a 4-byte pseudo-random challenge nonce (`esp_random()`) with a 30-second TTL.
4. The phone computes the 8-byte authentication proof: `SHA-256(secret[12] || challenge[4 LE] || sequence[2 LE])[:8]`.
5. The phone sends opcode `0x25` (`AUTHENTICATE`) with the 8-byte proof.
6. The ESP32 consumes the challenge immediately (defeating replay), computes the matching proof, and verifies in constant time.
7. Upon verification, the session is authorized for 5 minutes of activity (refreshed per command) and is bound to that specific connection handle: another concurrent connection cannot ride the session.

### 5.3 Negative Security Defenses
- **Proof-Less Session Rejection:** Physical presence alone never grants a session; opcode `0x25` requires a valid proof (`BLE_CFG_STATUS_UNAUTHORIZED = 4`).
- **Replay Attack Defense:** Every challenge is strictly single-use. Re-submitting the same proof without requesting a fresh challenge fails with `BLE_CFG_STATUS_INVALID_STATE = 5`.
- **Challenge Expiration:** Challenges expire after 30 seconds (`BLE_CFG_STATUS_INVALID_STATE = 5`).
- **Brute-Force Rate Limiting:** After 5 consecutive failed authentications, challenge requests are locked out for 3 seconds (`BLE_CFG_STATUS_BUSY = 3`).
- **Disconnect Invalidation:** Client disconnection immediately terminates the authenticated session, invalidates pending challenges and aborts staged transactions.
- **Connection Binding:** Sessions are valid only for the authenticating connection handle (`isAuthorizedFor(connId)`).
- **Configuration Reset vs Credentials:** `RESTORE_DEFAULTS` (`0x21`) resets lighting/decoder preferences but preserves owner credentials and stored captures. `CLEAR_DIAGNOSTICS` (`0x52`) clears only the event log.

---

## 6. Cross-Language Byte Test Vectors

The following deterministic test vector is published to verify cross-language byte serialization between Python (`test_ble_config_v2.py`), Android Kotlin (`ProtocolV2CodecTest.kt`), and C++ ESP32 firmware:

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
