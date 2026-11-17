# PulseShift

<p align="center">
  <img src="assets/pulseshift_logo_round.png" alt="PulseShift Logo" width="140" height="140">
</p>

PulseShift is an autonomous, passive CAN bus shift-light and diagnostic telemetry system running on the ESP32 for the 2025 Bajaj Pulsar NS125 ABS motorcycle.

---

## What Works

- **Autonomous Boot on Regulated Power**: Boots automatically on switched ignition 5V, operating without a smartphone, laptop, USB connection, or network.
- **Passive Listen-Only CAN Reception**: Receives 500 kbps motorcycle CAN traffic through hardware listen-only TWAI, forced recessive transceiver bias, and an air-gapped TX line to guarantee zero bus interference.
- **Field-Correlated RPM Estimation**: Decodes live candidate engine-speed frames from vehicle CAN traffic to drive real-time shift light indications.
- **6-LED Shift Controller**: Controls an addressable WS2812B array with boot sweep, progressive RPM stages, per-LED hysteresis, warning strobe, and owner commissioning status.
- **Preserved Capture and Logging**: Saves raw binary CAN frames and structured diagnostic events to LittleFS across dual log archives without risk of overwrite.
- **Optional BLE and OTA Updates**: Provides authenticated NimBLE GATT telemetry, configuration controls, and dual-partition signed OTA updates when paired with the companion Android app.
- **Bench Verification (2026-11-17)**: Updated firmware booted successfully on the real ESP32, and the owner reports the Android companion working with the controller. Physical strip illumination, motorcycle power wiring, and live OTA rollback still require separate hardware tests.

---

## Current Verification Status

| Domain | Verification Level | Current Status and Scope |
|---|---|---|
| **Software Core & CI** | Automated tests passed | Production firmware compiled; RMT, controller, BLE and OTA reliability regressions passed in software. |
| **USB Firmware Installation** | Real ESP32, verified 2026-11-17 | Firmware `4c7fc5b` flashed application-only to `app1` at `0x150000`; the running build stamp matched and the boot state was `VALID`. |
| **Capture & Settings Preservation** | Real ESP32, verified 2026-11-17 | Two 4 MiB flash backups matched, all 173 SHA-256 inventory entries verified, and 23,370 saved CAN records passed CRC16. Existing LittleFS, NVS owner credentials and diagnostic logs were preserved. |
| **Android Companion / BLE** | Owner-reported hardware bench test, 2026-11-17 | Owner reports the current Android app and updated ESP32 working together. Individual BLE operations were not independently logged here. |
| **LED Effect Logic / RMT** | Real ESP32, without LED strip | `RPM TEST 4500` selected logical LEDs 0-3; `RPM TEST 0` requested black on all six; RMT reported `TRANSMIT_OK`. Visible output was not observed. |
| **Real Motorcycle CAN** | Prior real-bike observation | 500 kbps CAN captured on 2025 Pulsar NS125 ABS; candidate `0x301` tracked engine speed. |
| **RPM Calibration** | Observational / uncalibrated | Effect 5 uses provisional x40.0 estimation, not a validated shift cue; the idle discrepancy remains. |
| **Physical WS2812B LED Strip** | Pending physical test | The strip was unavailable for the latest USB bench test. Actual illumination, GPIO27 DIN voltage, and waveforms remain unverified. |
| **Vehicle Power Harness** | Pending physical test | Motorcycle 12V-to-5V buck harness and inline fuse have not been validated in this bench session. |
| **Signed BLE OTA & Rollback** | Software-tested only | No live BLE OTA transfer, 120-second rollback, or OTA fault-injection test has been performed on the ESP32. |

The November 17 bench report and full-flash backups are stored privately on the owner's Windows PC, not in this repository. A `VALID` USB-flashed application does **not** constitute a successful OTA or rollback test. Do not commit raw flash images, owner credentials, or signing keys.

---

## Hardware and Essential Pinout

The core system comprises four hardware elements:
- **Microcontroller**: DOIT ESP32 DevKit V1 (ESP-WROOM-32).
- **CAN Transceiver**: SN65HVD230 3.3V transceiver with physical transmission inhibitors.
- **Display Strip**: 6 addressable WS2812B RGB LEDs with damping resistor and capacitor.
- **Power Supply**: Automotive-rated regulated 5V DC-DC buck converter from switched 12V.

| ESP32 Pin | Function | Connection Destination | Electrical Rule / Safety Constraint |
|---|---|---|---|
| **GPIO16 (RX2)** | TWAI RX | Transceiver CRX (Pin 4) | 3.3V digital input receiving reconstructed CAN bits. |
| **GPIO17 (TX2)** | TWAI TX | Physically Disconnected | Mandatory physical air gap; do not connect to transceiver CTX. |
| **3V3 Rail** | 3.3V Logic | Transceiver VCC (Pin 3) & CTX (Pin 1) | Powers transceiver; directly clamps CTX recessive (no resistor). |
| **GPIO27 (D27)** | LED Data | WS2812B DIN (First LED) | 3.3V NRZ via 330-470 ohm series resistor; optional 74AHCT125 buffer. |
| **VIN** | 5V Power | Regulated 5V Buck Output | Automotive-rated 5.0V input (max 5.5V); never connect 12V directly. |
| **GND** | Ground | Common System Ground | Common ground bonding ESP32, transceiver, LEDs, and power supply. |

For complete electrical schematics, noise filtering, and circuit protection, see [docs/HARDWARE.md](docs/HARDWARE.md).

---

## Quick Start (Windows / Arduino)

1. Install Arduino CLI with Arduino-ESP32 **3.3.12** (ESP-IDF 5.5.5) and
   `NimBLE-Arduino` **2.3.8**.
2. Follow the [build instructions](docs/TESTING.md#1-build-the-firmware-with-arduino-cli)
   for the **ESP32 DevKit V1** and the NVS startup guard. The default 4 MB
   layout has two 1.25 MiB app slots and a separate LittleFS partition.
3. Use the Serial Monitor at **115200 baud** with CRLF line endings.
4. On an existing device, use an approved application-only update procedure
   with a verified backup. Preserve the bootloader, partition table, NVS,
   and LittleFS.

---

## How It Operates Without a Phone

PulseShift operates as a completely autonomous embedded instrument on the motorcycle:

```text
Bike CAN Bus (500 kbps)
       |
  SN65HVD230 (CRX 3.3V out, CTX clamped to 3V3)
       |
  ESP32 TWAI (GPIO16 RX, GPIO17 air-gap)
       |
  RPM Decoder (0x301 byte 0, x40.0) ---> LittleFS Storage (Binary Capture & Logs)
       |
  LED Engine (Effect 5 Preview)
       |
  WS2812B Strip (GPIO27 DIN, 6 LEDs)
```

- **Autonomous Signal Pipeline**: Motorcycle CAN (500 kbps) -> SN65HVD230 -> ESP32 TWAI (GPIO16, listen-only) -> RPM Decoder -> LED Engine -> WS2812B strip (GPIO27).
- **Parallel Logging**: Decoded engine metrics and raw CAN frames stream concurrently to LittleFS flash storage and the diagnostic event buffer.
- **Power Architecture**: The ESP32 and LEDs require external regulated 5V power from switched ignition through a fused DC-DC converter. CAN bus wires (CAN-H and CAN-L) carry signaling only and do NOT supply power.
- **No Phone Dependency**: The unit boots, synchronizes to CAN traffic, computes RPM, and drives shift lights without Bluetooth, smartphones, laptops, or internet access. BLE is strictly optional for configuration and diagnostics.

---

## RPM Defaults and Limitations

- **CAN Bus Parameters**: Standard 11-bit identifier `0x301`, payload byte index `0`, bitrate `500,000 bit/s`, linear multiplier `40.0`, zero offset, and a `250 ms` freshness cutoff window.
- **Field-Correlated vs Calibrated**:
  - **Default Effect 5 (Provisional Preview)**: Enables an uncalibrated RPM preview out of the box using observational multiplier 40.0 across 6 LEDs. It operates without vehicle speed gating and is explicitly **not** an independently calibrated shift cue.
  - **Gated Effect 4 (Verified Shift Bar)**: The production shift light bar remains strictly locked until an authenticated multi-point calibration session against an independent tachometer calculates a valid cryptographic fingerprint.
  - **Documented Idle Discrepancy**: On the 2025 Pulsar NS125 ABS, raw byte values tracked engine activity (raw 0 engine off; raw 35-40 at idle = 1400-1600 RPM; raw 75 at ~3000 RPM agreeing with the dashboard). However, the dashboard needle showed ~1000 RPM at idle. Reference accuracy remains unresolved.
- For complete evidence logs and field analysis, see [docs/NS125-RPM-FIELD-EVIDENCE.md](docs/NS125-RPM-FIELD-EVIDENCE.md).

---

## Recording and Commands

The on-device flash currently preserves a **23,370-record (467,476-byte)** physical motorcycle CAN capture validated on PC. To prevent data loss, the firmware enforces a fail-closed policy: **new raw frames will not be written to flash** until a user explicitly clears the capture. Live CAN decoding and the provisional LED preview continue unaffected.

The command `CLEAR YES` permanently erases the on-device CAN capture and active diagnostic log. It is destructive and must never be executed automatically.

Connect over USB serial at 115200 baud to issue genuine diagnostic and maintenance commands:

| Command | Mode | Purpose and Operational Behavior |
|---|---|---|
| `STATUS` | Read-only | Prints TWAI controller health, LittleFS storage usage, and system state. |
| `RPM STATUS` | Read-only | Shows candidate 0x301 raw byte, estimated RPM, age, and freshness flag. |
| `DUMP STATUS` | Read-only | Reports current export cursor position, record count, and total pages. |
| `DUMP ALL` | Read-only | Streams entire binary CAN capture in paced serial chunks with watchdog feeds. |
| `LOGS` | Read-only | Dumps active and archived diagnostic logs (`/diag.log`, `/diag-prev.log`). |
| `CAPTURE STOP` | Control | Flushes pending frame buffers to flash and pauses active recording. |
| `CAPTURE RESUME` | Control | Resumes recording; rejected if flash holds preserved capture or is full. |
| `OTA STATUS` | Read-only | Displays running app partition (`app0`/`app1`), image state, and rollback status. |
| `HELP` | Read-only | Displays the interactive command menu and syntax reference. |

---

## Safety and Deeper Guides

### Critical Safety Rules
1. **Air-Gap GPIO17**: ESP32 GPIO17 must remain physically disconnected from the transceiver at all times to prevent accidental transmissions.
2. **Clamp CTX to 3V3**: Transceiver CTX must be wired directly to the 3.3V rail (no resistor) to force recessive bus state.
3. **Regulate 12V Power**: Never connect motorcycle 12V directly to ESP32 VIN, 3V3, or LED strips. Use a fused, automotive-rated 5V buck converter.

### Documentation Index
- [docs/HARDWARE.md](docs/HARDWARE.md) - Complete electrical specifications, schematics, pinouts, and automotive protection.
- [docs/BIKE-DEPLOYMENT-READINESS.md](docs/BIKE-DEPLOYMENT-READINESS.md) - Pre-installation checklist, power safety, and physical release gates.
- [docs/NS125-RPM-FIELD-EVIDENCE.md](docs/NS125-RPM-FIELD-EVIDENCE.md) - Candidate 0x301 field evidence, idle discrepancy analysis, and multiplier data.
- [docs/CAN-INVESTIGATION.md](docs/CAN-INVESTIGATION.md) - Historical TWAI driver analysis and simulator diagnostics.
- [docs/BLE-PROTOCOL.md](docs/BLE-PROTOCOL.md) - GATT architecture, 20-byte MTU framing, TLV schemas, and cryptographic auth.
- [docs/ANDROID-TERMINAL.md](docs/ANDROID-TERMINAL.md) - Android BLE terminal integration contract, opcode mappings, and UI rules.
- [docs/OTA-BENCH-TEST-PLAN.md](docs/OTA-BENCH-TEST-PLAN.md) - Dual-partition OTA validation, ECDSA P-256 signing, and rollback test plan.
- [docs/TESTING.md](docs/TESTING.md) - Interactive serial commands, automated regression suites, and test fixtures.
- [COPYRIGHT](COPYRIGHT) - Project copyright and licensing information.
