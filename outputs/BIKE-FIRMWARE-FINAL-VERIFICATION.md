# PULSESHIFT FIRMWARE VERIFICATION AND MOTORCYCLE RELEASE REPORT

**Date:** 16 November 2026
**Target Vehicle:** Bajaj Pulsar NS125 2025 ABS
**Repository:** https://github.com/iamaritrasaha/PulseShift
**Branch:** fix/motorcycle-readiness-and-recovery
**Baseline SHA:** 0a4530ab5efc623753b5301c3a65e41f8d68873f (PR #33 merged)
**Release Candidate SHA:** b97a3782b130e527b13ea32dbf57e62bf6a524a8
**GitHub Pull Request:** https://github.com/iamaritrasaha/PulseShift/pull/34
**Release Gate Verdict:** SOFTWARE READY / PHYSICAL ACCEPTANCE PENDING

---

## 1. Executive Summary

This release candidate hardens the PulseShift ESP32 firmware for autonomous on-bike operation without phone, PC, Wi-Fi, or internet dependency. It resolves reproducible P0/P1 defects in the asynchronous TWAI bus-off recovery state machine and LED shift-light button precedence, preserves the historical Session 3 motorcycle CAN capture and calibration baseline, verifies dual compilation (production and headless), and validates complete test suites.

---

## 2. Release Verification Matrix

| Required Field | Verification Findings and Evidence | Status |
|---|---|---|
| **Baseline SHA** | `0a4530ab5efc623753b5301c3a65e41f8d68873f` (PR #33) | VERIFIED |
| **Release Candidate SHA** | `b97a3782b130e527b13ea32dbf57e62bf6a524a8` (PR #34) | VERIFIED |
| **Vehicle Compatibility** | Bajaj Pulsar NS125 2025 ABS; 500,000 bps listen-only; CAN ID `0x301` byte 0, multiplier `40.0`, offset `0`; standard 11-bit identifier | PRESERVED |
| **Partition Scheme** | `default` (app0: 1.25 MB, app1: 1.25 MB, spiffs/littlefs: 1.43 MB) | MATCHED |
| **Fixed Defects** | 1. Asynchronous TWAI recovery bounded state machine<br>2. Shift light obscuration on BOOT button press<br>3. Serial diagnostic export cancellation | RESOLVED |
| **Host & Python Tests** | 104 passed, 4 skipped (shutil g++ skips on Windows). All unit, integration, and security checks GREEN | PASS |
| **Dual-Build Compilation** | Production: 772,053 bytes (58% flash)<br>Headless: 721,317 bytes (55% flash) | PASS |
| **Firmware Binary Artifact** | `build/PulseShift.ino.bin`<br>SHA-256: `781C341C3E337936BFC12C0A211AAE9C7CB47603674DC7E4AAEA59C156AA292C`<br>Size: 772,208 bytes | GENERATED |
| **Storage & Capture Preservation** | LittleFS mounted cleanly (618,496 used / 823,296 free). Session 3 capture (23,370 records, 467,476 bytes) intact and un-erased. PC backup verified | PRESERVED |
| **On-Device Bench Tests** | COM5 verified: ESP32-D0WD-V3 (rev 3.1), LittleFS mounted, TWAI 500 kbps listen-only, BLE advertising, `RPM TEST 3000` 10s bench test | BENCH VERIFIED |
| **WS2812B 6-LED Physical Strip** | Code and simulation verified; physical strip not attached to bench | NOT TESTED |
| **Motorcycle 12V Buck & CAN Bus** | Lab bench only; actual motorcycle harness, buck converter and fuse | NOT TESTED |
| **Android BLE OTA Verification** | Host protocol and cryptography verified; live phone interaction | NOT TESTED |

---

## 3. Detailed Defect Root Cause and Fix Analysis

### Defect 1: Asynchronous TWAI Bus-Off Recovery State Machine (P0)
- **Files Modified:** `PulseShift/src/can_transport.cpp`, `PulseShift/src/can_transport.h`, `test/stubs/driver/twai.h`, `test/test_transport.cpp`
- **Root Cause:** In the legacy ESP-IDF TWAI driver, bus-off recovery transitions asynchronously: `TWAI_STATE_BUS_OFF -> (twai_initiate_recovery()) -> TWAI_STATE_RECOVERING -> (128 occurrences of 11 recessive bits) -> TWAI_STATE_STOPPED -> (twai_start()) -> TWAI_STATE_RUNNING`. Previously, `CanTransport::attemptBusOffRecovery()` grouped `BUS_OFF` and `RECOVERING` together, repeatedly calling `twai_initiate_recovery()` while in `RECOVERING` (which returns `ESP_ERR_INVALID_STATE`) and prematurely incrementing `busOffRecoveryAttempts_` until the 5-attempt budget was exhausted before recovery could finish.
- **Fix:**
  1. Enabled `TWAI_ALERT_BUS_RECOVERED` in `general.alerts_enabled`.
  2. Bounded the state machine: `twai_initiate_recovery()` is invoked strictly once when in `TWAI_STATE_BUS_OFF`.
  3. While in `TWAI_STATE_RECOVERING`, `CanTransport` legitimately waits for the bus sequence without incrementing retry attempts (bounded by a 3000 ms timeout for stuck dominant bus lines).
  4. Once `TWAI_STATE_STOPPED` is reported, `twai_start()` is called to re-enter listen-only operation.
  5. Added unit tests verifying transitions: `RUNNING -> BUS_OFF -> RECOVERING -> STOPPED -> RUNNING`, no premature retry exhaustion, and recovery timeout handling.

### Defect 2: Shift Light Obscuration on BOOT Button Press (P1)
- **Files Modified:** `PulseShift/src/led_effects.cpp`, `test/test_led_effects.cpp`
- **Root Cause:** In `PulseShift.ino::handleButton()`, every physical button interaction calls `ConfigManager::instance().triggerAuthWindow(now);`, setting `input.authWindowOpen = true` for 60 seconds. In `LedEffectEngine::update()`, Priority 4 (owner commissioning cyan pulse) was evaluated before Priority 6 (RPM shift light rendering). When a rider short-pressed or held the button while riding (e.g. to cycle recording stage or pause capture), the shift light was completely masked with a cyan pulse for 60 seconds.
- **Fix:**
  1. In `LedEffectEngine::update()`, when an RPM display mode is active (`cfg.effect == 4 || cfg.effect == 5`) and fresh qualified engine-running RPM is received (`rpm.engineRunning && sampleQualified`), live RPM rendering takes precedence over the commissioning window.
  2. Tapping or holding the BOOT button while riding triggers immediate ~200 ms white feedback (Priority 2), then immediately returns to the RPM bar.
  3. When the engine is off (0 RPM / key-on engine-stopped) or on the bench, the cyan pulse continues to display normally for commissioning and owner pairing.
  4. Added regression tests in `test/test_led_effects.cpp` covering button presses during active running RPM, transition between running and stopped engine states, and auth window expiry.

### Defect 3: Diagnostic Serial Export Cancellation (P1)
- **Files Modified:** `PulseShift/src/diag_logger.cpp`
- **Root Cause:** `DiagLogger::dumpToSerial()` dumps up to 256 KiB of logs across archive and active files in a blocking loop that cannot be interrupted by the user.
- **Fix:** Added user cancellation check (`q`, `Q`, `ESC`, `Ctrl+C`) via `Serial.peek()` inside the read loop, immediately aborting the export and closing the file handle if requested, while retaining task WDT feeds.

---

## 4. Hardware Safety and Preflight Checklist

Before connecting the ESP32 to the Bajaj Pulsar NS125 2025 ABS:

1. **Power Regulation (MANDATORY):**
   - The diagnostic port carries unconditioned 12V motorcycle battery power with cranking dips and alternator spikes.
   - **NEVER** connect motorcycle 12V directly to ESP32 pins (VIN, 5V, 3V3).
   - Use an automotive-rated buck converter stepping 12V down to clean 5V (regulated), protected with a 500mA inline fast-acting fuse.
2. **Passive CAN Isolation (MANDATORY):**
   - ESP32 GPIO16 connects to Transceiver CRX/RXD.
   - ESP32 GPIO17 must remain **PHYSICALLY DISCONNECTED (air-gap)**.
   - Transceiver CTX/TXD must be tied directly to ESP32 3V3 rail to guarantee physical recessive state.
   - Transceiver powered at 3.3V (VCC -> 3V3, GND -> GND).
3. **CAN Termination Check:**
   - Measure resistance between CAN-H and CAN-L with the motorcycle completely powered off.
   - The vehicle bus already provides termination (~60 ohms nominal from dual 120-ohm resistors in ECU/cluster).
   - Do NOT add a third 120-ohm terminating resistor across the bus. Verify transceiver breakout board jumper/resistor configuration.
4. **Physical Installation:**
   - Enclose in a water-resistant, vibration-damped casing.
   - Ensure wiring harness has adequate strain relief and does not interfere with motorcycle steering head, throttle cable, or front suspension movement.
