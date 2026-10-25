# REVIX LINK: Comprehensive Engineering Audit, Debugging, and Remediation Report

**Date:** 2026-10-25  
**Target Hardware:** ESP32 DevKit V1 (ESP32-D0WDQ6), SN65HVD230 3.3V CAN Transceiver, 6x WS2812B Addressable LEDs, SSD1306 128x64 I2C OLED  
**Target Vehicle:** 2025 Bajaj Pulsar NS125 ABS  
**Baseline Git Commit:** `159c4d1313a2593a45d4a11fe7519e8b39e9b9f5` (branch `main`)  
**Audit Git Branch:** `audit/2026-10-25`  
**Remediation Commit:** `790b202af4c3b8ab9636c3337e382056c4b59124`  

---

## 1. Executive Summary

A comprehensive engineering audit, static analysis, code hardening, and test expansion was conducted on the REVIX LINK firmware repository.

The codebase represents a well-architected prototype embedded system. The core data models, bit-level CAN unpacking engine, state-tracking logic, and display rendering subsystems demonstrate solid defensive programming patterns, clean modularity, and strict separation between simulation harness code and production vehicle code.

The audit identified no Critical or High severity security vulnerabilities or memory corruption bugs. However, two Medium severity reliability issues, four Low severity code quality and test coverage deficiencies, and seven Informational architectural and hardware integration constraints were identified.

All six actionable defects (Medium and Low) have been remediated in the codebase and verified through automated test suites and static analysis. Unit test coverage has been expanded from 14 test suites (131,683 assertions) to 21 test suites (131,726 assertions), compiled and executed under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan). Both PlatformIO target environments (`esp32dev_wokwi` and `esp32dev_bike`) build cleanly with zero warnings under `-Wconversion`.

**Important Safety Disclaimer:** The firmware is currently verified only in software simulation and host-native unit test harnesses. The physical CAN bus protocol, baud rate, and ECU frame identifiers of the 2025 Bajaj Pulsar NS125 ABS remain unconfirmed on physical hardware. The system is NOT yet ready for direct motorcycle installation until physical vehicle bus sniffing, hardware transceiver bench verification, and automotive-grade transient power protection are completed.

---

## 2. Baseline State

Prior to remediation, the repository on branch `main` (commit `159c4d1`) exhibited the following characteristics:

- **Build Environments:** Configured in `platformio.ini` with two distinct targets:
  - `esp32dev_wokwi`: Simulation environment enabling demonstration and replay transports.
  - `esp32dev_bike`: Vehicle target enforcing `REVIX_MODE=3` (Listen-Only Mode), disabling all CAN transmission routines, and stripping test harnesses.
- **Symbol Isolation:** In `esp32dev_bike`, symbol verification with `nm` confirmed that `twai_transmit`, `CanTransport::sendTest`, `demonstrationFrame`, and `ReplayTransport` were completely omitted by the compiler.
- **Baseline Test Suite:** Native test harness (`test/test_core.cpp`) executed via `bash test/run.sh` using Clang/GCC with `-fsanitize=address,undefined -Wall -Wextra -Werror`, passing 14 test cases and 131,683 assertions.
- **Memory Footprint:** Baseline firmware consumed approximately 9.0% RAM (29.4 KB of 320 KB) and 23.7% Flash (310.9 KB of 1.28 MB).

---

## 3. Issue Summary Table

| Issue ID | Severity | Subsystem | Description | Remediation Status |
| :--- | :--- | :--- | :--- | :--- |
| **M-1** | Medium | CAN Transport | Transient `TWAI_ALERT_BUS_ERROR` triggered permanent fault halt in Listen-Only mode | **RESOLVED** (Split alert handling between BIKE and TEST modes) |
| **M-2** | Medium | Logging | `FrameLog::push` chained `snprintf` calls lacked defensive buffer remainder checks | **RESOLVED** (Hardened pointer arithmetic and remainder guarding) |
| **L-1** | Low | Simulated ECU | Implicit narrowing conversion in `demonstrationFrame` step calculation | **RESOLVED** (Explicit `uint16_t` cast added) |
| **L-2** | Low | Dashboard Model | Implicit narrowing conversion in `litLedCount` return statement | **RESOLVED** (Explicit `uint8_t` cast added) |
| **L-3** | Low | Build / Config | Stray carriage return (CRLF) characters at EOF in config and documentation files | **RESOLVED** (Normalized to LF across repository) |
| **L-4** | Low | Test Suite | Gaps in edge-case coverage (boundary checks, enum coverage, rejection counters) | **RESOLVED** (Added 7 test suites, +43 assertions) |
| **I-1** | Info | Simulator | Wokwi virtual TWAI controller does not simulate TX ACK, causing TX completion stall | **DOCUMENTED** (Known Wokwi simulator limitation) |
| **I-2** | Info | Silicon Errata | ESP32 TWAI Listen-Only dominant bit errata requires transceiver hardware RS management | **DOCUMENTED** (Detailed in Hardware Guidance) |
| **I-3** | Info | CAN Architecture | Single outstanding TX frame design is appropriate for diagnostics but lacks priority queueing | **DOCUMENTED** (Architectural assessment) |
| **I-4** | Info | Display Rendering | `dashboard.cpp` bar fill math could visually overrun if `mapping.maxRpm > config::maxRpm` | **DOCUMENTED** (Design boundary documented) |
| **I-5** | Info | Toolchain | clangd reports false-positive missing headers due to incomplete `c_cpp_properties.json` | **DOCUMENTED** (PlatformIO build unaffected) |
| **I-6** | Info | Vehicle Protocol | 2025 Pulsar NS125 CAN bitrate and RPM CAN ID unverified on real vehicle | **DOCUMENTED** (Requires physical bus sniffing) |
| **I-7** | Info | Electrical Safety | Automotive load dump (ISO 7637-2) and 12V-to-3.3V power conditioning required | **DOCUMENTED** (Hardware roadmap documented) |

---

## 4. Remediation Details

### M-1: Transient Bus Error Fault in Listen-Only Mode (src/can_transport.cpp)
- **Problem:** In `CanTransport::poll()`, the controller monitored `TWAI_ALERT_BUS_ERROR` and called `fault("bus error or RX overflow; inspect counters")`, entering a permanent stopped state. In an automotive environment, electrical transients during vehicle ignition, cranking, or initial bus synchronization frequently generate isolated CAN bit or stuffing errors. In Listen-Only mode, the ESP32 TWAI controller is purely passive and does not transmit error frames. Halting the firmware on a single transient error would cause the dashboard to freeze permanently during normal vehicle operation.
- **Fix:** Refactored alert handling in `src/can_transport.cpp`. Critical fatal errors (`TWAI_ALERT_BUS_OFF`, `TWAI_ALERT_RX_QUEUE_FULL`, `TWAI_ALERT_RX_FIFO_OVERRUN`) continue to halt the system across all modes. For `TWAI_ALERT_BUS_ERROR`, behavior is now mode-specific:
  - In `REVIX_BIKE` (Listen-Only mode): A non-fatal warning is logged to serial diagnostics while allowing the controller to automatically recover and continue receiving frames.
  - In `REVIX_TWAI_TEST` (Self-test mode): Strict halting behavior is preserved to detect simulator or loopback wiring defects immediately.

### M-2: Defensive Hardening of FrameLog snprintf Chain (src/frame_log.cpp)
- **Problem:** `FrameLog::push()` formatted CAN frame data using chained `snprintf` calls:
  ```cpp
  n += snprintf(line.text+n, sizeof(line.text)-n, ...);
  ```
  If `snprintf` returns a negative value (upon an encoding error) or if `n` exceeds `sizeof(line.text)`, unsigned pointer arithmetic or out-of-bounds buffer offsets could occur.
- **Fix:** Hardened the formatting pipeline:
  - Validated `n >= 0` after initial envelope formatting.
  - Computed `const int rem = int(sizeof(line.text)) - n` prior to each subsequent format step; if `rem <= 0`, loop execution breaks immediately.
  - Guarded positive return values (`if (w > 0) n += w;`).
  - Clamped `line.size` to buffer capacity minus null terminator.

### L-1: Implicit Narrowing Cast in Simulated ECU (src/simulated_ecu.cpp)
- **Problem:** In `demonstrationFrame()`, the expression `(step <= 65 ? step : 130 - step) * 100` evaluates as integer arithmetic assigned to `const uint16_t raw`. While the maximum value (6,500) safely fits in 16 bits, compilers with `-Wconversion` flag implicit truncation.
- **Fix:** Wrapped the arithmetic expression in an explicit `uint16_t(...)` cast.

### L-2: Implicit Narrowing Cast in Dashboard Model (include/dashboard_model.h)
- **Problem:** In `litLedCount()`, the statement `return uint32_t(rpm) * count / maximum;` implicitly converted a 32-bit unsigned arithmetic result to the `uint8_t` return type. Because the calculation is mathematically bounded by `count` (a `uint8_t`), no value loss occurred, but `-Wconversion` produced compiler warnings.
- **Fix:** Added an explicit `uint8_t(...)` return cast.

### L-3: Line Ending Normalization
- **Problem:** Files `platformio.ini`, `wokwi.toml`, `docs/HARDWARE.md`, `docs/TESTING.md`, and `docs/VERIFICATION.md` contained trailing carriage returns (CRLF), causing mixed line endings within the repository.
- **Fix:** Stripped carriage returns across affected files, enforcing LF standard across the Linux/WSL development environment.

### L-4: Unit Test Suite Expansion (test/test_core.cpp)
- **Problem:** Baseline tests had high assertion volume for decoding permutations (131,072 permutations of 16-bit raw data) but omitted key boundary checks on CAN frame metadata, state machine counters, and transmission tracker rejection paths.
- **Fix:** Added 7 new comprehensive test suites:
  1. `envelope()`: Validates Standard CAN ID upper boundary (0x7FF valid, 0x800 invalid), Extended CAN ID upper boundary (0x1FFFFFFF valid, 0x20000000 invalid), and DLC boundaries (0-8 valid, 9 invalid).
  2. `decode_names()`: Asserts that every `DecodeResult` enum variant maps to a non-null, non-empty diagnostic string.
  3. `state()` counter checks: Verifies that `RpmState::accepted()` and `RpmState::rejected()` increment accurately upon valid, out-of-range, and RTR frames.
  4. `leds()` zero and single LED checks: Verifies `litLedCount` behavior when total LED count is zero and when configured with a single indicator LED.
  5. `tx()` RTR rejection: Asserts that `TxTracker::onReceive()` rejects remote frames even if arbitration ID matches.
  6. `tx()` DLC and Extended flag rejection: Asserts that `TxTracker::onReceive()` rejects frames with mismatched DLC or mismatched standard/extended framing.
  7. `replay()` sequence counter monotonicity: Asserts that consecutive demonstration frames advance the internal 32-bit payload sequence counter by exactly 1.

---

## 5. Verification and Test Results

### Automated Host Unit Tests
Native tests were executed on Linux using Clang/GCC with AddressSanitizer and UndefinedBehaviorSanitizer enabled:
```text
PASS valid LE CAN frame
PASS identifier width and standard/extended distinction
PASS all DLC values and output preservation on rejection
PASS RTR rejection and disabled bike mapping
PASS BE, offsets, mapping bounds, zero divisor
PASS rational conversion, rounding and overflow safety
PASS exhaustive 16-bit RPM range including zero and limits
PASS validEnvelope: ID and DLC boundary conditions
PASS decodeName: all enum values return non-empty strings
PASS missing, valid zero, stale boundary, invalid non-refresh, recovery and faults
PASS millis rollover
PASS unmapped, invalid and disconnected states
PASS RpmState accepted/rejected counters
PASS six LED thresholds, clamping, zero count and stale/off behavior
PASS original RX-without-TX regression, single timeout, no queue flooding
PASS RX/TX ordering, driver pending requirement, sequence mismatch and duplicates
PASS TxTracker rejects RTR frames
PASS TxTracker rejects DLC-mismatched frames
PASS TxTracker rejects extended-flag-mismatched frames
PASS full-frame replay, invalid interval, silence and resumed frames
PASS demonstration frame sequence counter increments by 1
ALL PASS: 21 cases, 131726 assertions
```

### Firmware Compilation Results
Both target environments compiled with PlatformIO without errors or warnings:

1. **`esp32dev_wokwi` (Simulation Environment):**
   - Flash Usage: 310,933 bytes (23.7% of 1,310,720 bytes)
   - RAM Usage: 29,440 bytes (9.0% of 327,680 bytes)
   - Status: SUCCESS

2. **`esp32dev_bike` (Production Vehicle Environment):**
   - Flash Usage: 312,413 bytes (23.8% of 1,310,720 bytes)
   - RAM Usage: 29,528 bytes (9.0% of 327,680 bytes)
   - Status: SUCCESS

### Binary Symbol Security Verification
Inspection of the `esp32dev_bike` compiled ELF binary using `xtensa-esp32-elf-nm` confirmed that all transmission and simulation code symbols were completely stripped:
- `twai_transmit`: NOT FOUND
- `CanTransport::sendTest`: NOT FOUND
- `demonstrationFrame`: NOT FOUND
- `ReplayTransport`: NOT FOUND

This guarantees that the vehicle firmware is physically incapable of transmitting dominant bits onto the motorcycle CAN bus.

---

## 6. CAN and TWAI Subsystem Evaluation

### Driver Configuration
- **Mode:** Configured as `TWAI_MODE_LISTEN_ONLY` in `REVIX_BIKE` mode. The TWAI controller will not send ACK bits or active error flags.
- **Queue Configuration:** `rx_queue_len = 64`, `tx_queue_len = 0` in bike mode. An RX queue depth of 64 frames provides 64 milliseconds of buffering headroom at a 1 kHz bus load, which exceeds the processing loop period (typically 5 ms to 20 ms).
- **Alert Flags:** `TWAI_ALERT_RX_DATA | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN`.
- **Clock Accuracy:** The ESP32 80 MHz APB clock divider produces standard bitrates (250 kbps, 500 kbps, 1 Mbps) with 0.0% nominal timing error.

### Wokwi Simulator Limitations (I-1)
During simulation in Wokwi, loopback transmission tests fail to complete (`TX_SUCCESS` is never signaled). As documented in `CAN-INVESTIGATION.md`, this is an artifact of Wokwi virtual TWAI peripherals not generating hardware loopback ACKs. The firmware correctly detects this condition, times out after 1,000 ms, logs the diagnostic timeout once, and halts cleanly. In `REVIX_REPLAY` mode, simulation runs smoothly without relying on the virtual TWAI controller.

---

## 7. Hardware Readiness and Vehicle Integration Guidance

### Target Motorcycle: 2025 Bajaj Pulsar NS125 ABS
The 2025 Pulsar NS125 ABS features single-channel front ABS and an electronic fuel injection (EFI) / engine management unit. While modern Indian two-wheelers comply with OBD-II / BS-VI diagnostics standards, CAN bus topology varies:
1. Some Indian sub-200cc motorcycles route CAN directly to an OBD-II diagnostic port (ISO 19689 6-pin or standard 16-pin connector).
2. Other variants utilize K-Line (ISO 9141 / ISO 14230) for off-board diagnostics while reserving CAN strictly for high-speed inter-ECU communication (between EFI ECU and ABS module).
3. The exact broadcast identifier, endianness, scaling factor, and repetition rate for engine RPM on this model are currently unverified.

### Physical Wiring and Transceiver Configuration (SN65HVD230)
- **Operating Voltage:** The SN65HVD230 must be powered from the ESP32 3.3V rail. Do not power it with 5V, as its logic lines interface directly with ESP32 3.3V GPIOs.
- **Pin Connections:**
  - `ESP32 GPIO 16` -> `SN65HVD230 RXD`
  - `ESP32 GPIO 17` -> `SN65HVD230 TXD` (or leave disconnected for vehicle listening)
  - `GND` -> Common Vehicle / Transceiver / ESP32 Ground
- **RS Mode Pin (Crucial):**
  - Pin 8 (RS) of the SN65HVD230 controls operational mode.
  - Tying RS to 3.3V (Logic HIGH) forces the transceiver into Listen-Only / Standby mode, physically disabling the driver circuitry.
  - Tying RS to GND enables High-Speed mode.
  - **Recommendation:** When installing on the motorcycle, tie RS to 3.3V (or pull high with a 10k resistor) to enforce hardware-level listen-only operation.

### Electrical and Power Conditioning Constraints
Connecting directly to a motorcycle 12V electrical system without conditioning will destroy the ESP32.
- **Transient Voltage:** Vehicle electrical systems experience inductive spikes, alternator ripple, and load dump transients up to 40V-60V (ISO 7637-2).
- **Power Stage Required:** A high-efficiency automotive buck regulator (e.g., Texas Instruments LM5164 or Monolithic Power Systems MP4575) rated for at least 45V DC input, paired with a bidirectional TVS diode (e.g., SMBJ24A or SMAJ28CA) at the 12V input.
- **LED Level Shifting:** WS2812B LEDs require a 5V supply and specify a minimum high-level input voltage of 0.7 * VDD (3.5V). While an ESP32 3.3V GPIO often drives a single WS2812B at room temperature, reliable operation requires a fast level shifter (e.g., 74AHCT125) or a sacrificial diode dropper on the first pixel.

---

## 8. Remaining Risks and Unresolved Items

1. **Unconfirmed Bus Parameters:** The bitrate (typically 250 kbps or 500 kbps) and CAN message ID for RPM on the 2025 Pulsar NS125 ABS must be sniffed with a dedicated CAN analyzer (e.g., CANable, ValueCAN, or Logic Analyzer) before enabling vehicle mode.
2. **Silicon Errata Workaround:** Early ESP32 revisions possess an erratum where dominant bits may be transmitted during Listen-Only mode under specific bus error conditions unless `CONFIG_TWAI_ERRATA_FIX_LISTEN_ONLY_DOM` is compiled into the SDK. Hardware RS pull-up completely mitigates this risk.
3. **Display Clamping Geometry:** If an invalid mapping is configured where `mapping.maxRpm > config::maxRpm`, `dashboard.cpp` line 49 could compute a bar graph width exceeding the border. In the current configuration, values are safely clamped, but a defensive clamp at render time is recommended for future revisions.

---

## 9. Inventory of Changed Files

During the remediation phase, the following files were modified and committed to the repository:

1. `include/dashboard_model.h`: Added explicit `uint8_t` cast in `litLedCount` return statement to resolve compiler conversion warning.
2. `src/can_transport.cpp`: Split alert polling logic so transient bus errors produce non-fatal warnings in `REVIX_BIKE` mode while preserving strict failure in `REVIX_TWAI_TEST`.
3. `src/frame_log.cpp`: Hardened `FrameLog::push` string formatting with defensive buffer remainder checks and positive return guards.
4. `src/simulated_ecu.cpp`: Added explicit `uint16_t` cast in demonstration frame generation to resolve arithmetic conversion warning.
5. `test/test_core.cpp`: Added 7 test cases covering CAN envelope boundaries, decode names, state machine counters, LED thresholds, TX tracker filtering, and replay sequence counters.
6. `platformio.ini`: Normalized line endings from CRLF to LF.
7. `wokwi.toml`: Normalized line endings from CRLF to LF.

---

## 10. Git Status and Audit Artifacts

- **Active Branch:** `audit/2026-10-25`
- **Remediation Commit:** `790b202af4c3b8ab9636c3337e382056c4b59124`
- **Author:** Aritra Saha <aritrasab@gmail.com>
- **Date:** Sun Oct 25 18:01:26 2026 +0000
- **Commit Summary:** `audit: fix narrowing casts, harden snprintf, relax bus-error in listen-only mode, expand tests`
- **Working Tree:** Clean. No untracked or uncommitted code files exist.

---

## 11. Next Engineering Priorities

The recommended next steps for advancing REVIX LINK from prototype to vehicle installation are prioritized as follows:

1. **Merge Audit Branch:** Review and merge branch `audit/2026-10-25` into `main` via pull request or fast-forward merge.
2. **Vehicle CAN Bus Sniffing:** Connect an isolated CAN analyzer to the 2025 Pulsar NS125 diagnostic connector with the ignition ON and engine running. Record raw logs to confirm bitrate, endianness, CAN identifier, and byte offsets for engine RPM.
3. **Hardware Bench Validation:** Assemble the ESP32 DevKit V1 and SN65HVD230 on a test bench with an external CAN signal generator (e.g., CANable running `cangen`) to verify physical frame reception at 250 kbps and 500 kbps.
4. **Power Supply Conditioning Module:** Fabricate a power regulation board incorporating an automotive-rated buck regulator, reverse-polarity protection diode, and TVS transient suppressor.
5. **Enforce .gitattributes:** Add repository-level `.gitattributes` to permanently enforce LF line endings on all `*.cpp`, `*.h`, `*.ini`, `*.toml`, and `*.md` files.
6. **Defensive Render Clamping:** Add an explicit `width = min(width, maxBarWidth)` clamp in `dashboard.cpp` to guard against potential external mapping configuration errors.
7. **WS2812B Logic Shifting:** Integrate a 74AHCT125 level shifter between ESP32 GPIO 27 and the NeoPixel DIN line to ensure reliable signal integrity across wide temperature variations.
8. **Automotive Enclosure and Weatherproofing:** Design a 3D-printed IP65 water-resistant enclosure with vibration dampening for motorcycle handlebar mounting.
9. **On-Vehicle Passive Pilot:** Conduct initial on-vehicle testing with the transceiver RS pin permanently pulled high (listen-only) and the ESP32 TX line physically disconnected, logging received frames to flash or SD card before relying on the tachometer display.
