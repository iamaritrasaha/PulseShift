# PulseShift Firmware Testing & Verification Guide

This guide describes verification testing for PulseShift: interactive Arduino Serial Monitor diagnostic commands and automated regression test suites.

## 1. Primary Development Workflow: Arduino IDE
The production firmware is completely self-contained C++ built in standard Arduino IDE:
- Open `PulseShift/PulseShift.ino` in Arduino IDE.
- Board: **ESP32 Dev Module**
- Partition Scheme: **Default 4MB with spiffs** (or LittleFS)
- Serial Monitor: **115200 baud**

No Python, PlatformIO, or WSL runtime is required for firmware compilation or field operation.

## 2. Interactive Serial Monitor Commands (115200 baud)

| Command | Purpose | Example / Notes |
|---|---|---|
| `HELP` | Displays command reference menu | `HELP` |
| `STATUS` | Prints complete diagnostic snapshot | Status of storage, LittleFS, CAN controller, and RPM candidate |
| `LOGS` | Dumps persistent diagnostic event log | Prints `/diag_events.log` from LittleFS |
| `RPM STATUS` | Candidate 0x301 engine-speed snapshot | Shows raw value, provisional estimated RPM, freshness, range |
| `RPM MONITOR ON` | Enable 10 Hz rate-limited candidate stream | Prints `CAND,timestamp,id,raw,est_rpm,fresh` |
| `RPM MONITOR OFF` | Disable candidate streaming | Silences rate-limited candidate output |
| `RPM MONITOR` | Single candidate snapshot | Instantaneous stream line |
| `RPM TEST <val>` | Bench-test 6-LED shift lights for 10s | `RPM TEST 4500` (valid: 0..20000; physical CAN shift lights remain disabled) |
| `RPM TEST OFF` | Immediately turn off shift-light test | Restores field capture status LEDs |
| `RPM MULTIPLIER <val>` | Update candidate provisional multiplier | `RPM MULTIPLIER 40.0` (default: 40.0; calibration remains UNVERIFIED) |
| `STAGE <1\|2\|3>` | Set active field stage | `STAGE 1` (KeyOn), `STAGE 2` (Idle), `STAGE 3` (Sweeps) |
| `1`, `2`, `3` | Quick shortcuts to switch stages | Triggers white LED confirmation flash |
| `CAPTURE STOP` | Flush memory buffers and pause flash | Safely commits queued records |
| `CAPTURE RESUME` | Resume flash recording | Resumes active capture file |
| `STREAM ON` | Enable live raw CAN frame streaming | Real-time USB Serial stream of received CAN frames |
| `STREAM OFF` | Disable live raw streaming | Default mode to prevent Serial Monitor lag |
| `DUMP` | Export CAN capture in 500-record pages | Reads binary capture from LittleFS |
| `DUMP NEXT` | Export next 500-record page | Advances cursor by 500 records |
| `DUMP ALL` | Continuous export of all records | Unpaginated full capture export |
| `DUMP STATUS` | Show export progress and page count | Shows cursor record index and total pages |
| `DUMP CANCEL` | Reset export cursor to Page 1 | Resets cursor |
| `DUMP PAGE <n>` | Export specific page number n | `DUMP PAGE 3` |
| `CLEAR` | Prompt with instructions to clear | Shows warning requiring `CLEAR YES` |
| `CLEAR YES` | Confirm erase and reinitialize | Formats storage and increments session ID |

## 3. Host-Side C++ and Integration Tests
Automated regression tests run locally with:
```sh
bash test/run.sh
```
The test suite validates:
- CAN envelope validation, RTR rejection, and extended frame handling.
- Binary record format (76-byte header, 20-byte records) and CRC16-CCITT integrity.
- Power-loss safe LittleFS capture, EOF unaligned remainder truncation, and rollback safety.
- Shift-light progression, RGB scaling, and alert modes.
- Candidate 0x301 monitor bounds, NaN/Inf rejection, and 32-bit timer rollover safety.
