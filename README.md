# PulseShift

Customizable ESP32 motorcycle progressive shift-light and offline CAN diagnostic system.

Designed for the **Bajaj Pulsar NS125 FI ABS (2025)** and general single-cylinder fuel-injected motorcycles using **ESP32 DevKit V1**, six **WS2812B addressable LEDs**, and an **SN65HVD230 CAN transceiver**.

---

## 1. Project Architecture & Unified Layout

PulseShift is organized around a **single authoritative source of truth** that works natively with both the Arduino IDE / Arduino CLI and PlatformIO:

```text
PulseShift-Firmware/
├── PulseShift/                       # Authoritative Arduino Sketch Directory
│   ├── PulseShift.ino               # Arduino sketch entry point
│   ├── UserConfig.h                 # Primary user configuration file
│   ├── ConfigCompat.h               # Configuration compatibility validation
│   └── src/                         # All firmware source code (.cpp and .h)
│       ├── main.cpp                 # Main setup() and loop() orchestration
│       ├── can_transport.cpp/.h     # ESP32 TWAI driver (Listen-Only, error tracking)
│       ├── can_recorder.cpp/.h      # Binary raw CAN recorder, CRC16, flush cadence
│       ├── storage_manager.cpp/.h   # LittleFS non-destructive mount & NVS preferences
│       ├── diag_logger.cpp/.h       # Persistent circular diagnostic log (/diag.log)
│       ├── button_handler.h         # 50ms debounced GPIO0 BOOT-button FSM
│       ├── serial_protocol.cpp/.h   # Interactive serial command protocol
│       ├── dashboard.cpp/.h         # 6-LED field status display & optional OLED
│       ├── shift_light.cpp/.h       # Progressive shift-light animation engine
│       ├── rpm_decoder.cpp/.h       # Fail-closed vehicle RPM frame decoder
│       ├── vehicle_profile.cpp/.h   # Pulsar NS125 2025 FI ABS candidate profile
│       └── simulated_ecu.cpp/.h     # Standalone demo synthetic frame generator
├── tools/
│   └── can_investigate.py           # Offline CAN log signal analyzer
├── test/
│   ├── test_offline_diagnostics.py  # 17-test Python functional integration suite
│   ├── run.sh                       # Linux host test runner
│   └── *.cpp                        # Component unit tests
├── .github/workflows/ci.yml         # GitHub Actions automated build & test CI
├── build.ps1                        # Native Windows multi-target compilation
├── test-firmware.ps1                # Comprehensive 22-test automated test harness
├── export-logs.ps1                  # Native Windows 11 PowerShell log export tool
├── export_logs.py                   # Cross-platform Python log export tool
├── Open-Arduino.ps1                 # One-click launcher for Arduino IDE 2.3.10
├── diagram.json / wokwi.toml        # Wokwi simulation configuration
└── README.md                        # Master project documentation
```

---

## 2. Hardware Configuration & Wiring

### Hardware Checklist
- **ESP32 DevKit V1** (ESP32-D0WDQ6 / ESP32-WROOM-32, 4 MB SPI Flash)
- **SN65HVD230 3.3V CAN transceiver breakout module**
- **6-pixel WS2812B RGB LED strip** (5V, GRB / 800 kHz)
- **330Ω series resistor** (for LED DIN signal line on GPIO27)
- **1000 µF, 10V electrolytic capacitor** (across LED +5V and GND)
- **Regulated 5V DC-DC buck converter** (or motorcycle 5V USB port)
- **Digital Multimeter** (for resistance and voltage validation)

---

### Hardware-Enforced Passive Safety Architecture

To guarantee that PulseShift can **NEVER transmit onto the motorcycle bus or interfere with the ECU/ABS**, the hardware setup uses a **dual-layer hardware-enforced passive listening architecture**:

#### Critical Safety Invariants (SN65HVD230 Transceiver)

1. **GPIO17 (TXD) Physical Isolation & Passive Recessive Holding:**
   - **DO NOT connect ESP32 GPIO17 to Transceiver Pin 1 (D / TXD).**
   - Leave ESP32 GPIO17 **physically disconnected** (no wire, no jumper).
   - Tie Transceiver Pin 1 (`D`) directly to **3.3V (`VCC`)** through an external 4.7kΩ to 10kΩ pull-up resistor (or direct to 3.3V).
   - *Datasheet Verification:* Per Table 1 (Driver Function Table) of the TI SN65HVD230 datasheet, when $D = \text{HIGH}$, the differential driver is forced permanently into the **RECESSIVE** state ($V_{OD(R)} \approx 0\text{ V}$, $V_{CANH} \approx V_{CANL} \approx 2.3\text{ V}$). The driver CANNOT physically drive a dominant bit onto the bus, regardless of MCU state, crashes, or reboot loops.
   - The firmware additionally initializes GPIO17 as a High-Z input with internal pull-up as a redundant software safeguard.

2. **Transceiver High-Speed Mode for 500 kbps (Pin 8 `Rs` tied to GND):**
   - Connect Transceiver Pin 8 (`Rs`) to **GND** ($V_{Rs} < 1.2\text{ V}$, $R_s = 0\Omega$).
   - *Datasheet Mode Analysis:*
     - Applying $V_{Rs} > 0.75 V_{CC}$ (Pin 8 HIGH) places the SN65HVD230 into **Low-Power Standby Mode**, which throttles receiver bias current, increases propagation delay, and degrades pulse skew. Standby mode can compromise continuous decoding of 500 kbps CAN traffic.
     - Applying $V_{Rs} < 1.2\text{ V}$ (Pin 8 LOW / GND) selects **Normal High-Speed Mode**, providing full receiver bandwidth, minimal propagation delay ($t_{PLH}, t_{PHL} \le 50\text{ ns}$), and pulse skew $\le 10\text{ ns}$, perfectly meeting the 2000 ns bit-timing constraints of 500 kbps CAN.
     - *Breakout Board Compatibility:* Common blue SN65HVD230 breakout boards already tie `Rs` to GND via an onboard 0Ω SMD jumper. **No trace cutting or desoldering of Pin 8 is required.**

3. **Onboard 120Ω Termination Resistor Removal (MANDATORY):**
   - An active motorcycle CAN network is **already terminated** with two 120Ω resistors (one in the ECU, one in the instrument cluster), yielding an equivalent resistance of 60Ω across CAN_H and CAN_L.
   - Most SN65HVD230 breakout boards include an onboard 120Ω SMD resistor (often labeled `120` or `R2`).
   - If connected without modification, the three parallel resistors reduce bus resistance to $40\Omega$ ($120 \parallel 120 \parallel 120 = 40\Omega$), overloading vehicle transceivers and risking ECU bus-off faults.
   - **You MUST remove/desolder the onboard 120Ω resistor on the breakout board** before connecting to the motorcycle harness. Measure with a multimeter across CAN_H and CAN_L when disconnected: it must read open circuit (>100 kΩ).

4. **Harness Resistance Verification (Before Connecting):**
   - With the motorcycle ignition **OFF**, measure resistance between CAN High and CAN Low on the diagnostic connector using a multimeter.
   - It **MUST read approximately 60Ω (55Ω–65Ω)**. If it reads 120Ω or open circuit, do not proceed until the diagnostic connector pinout is re-verified.

---

### Wiring Pinout Table

| ESP32 Pin | Transceiver Pin | Harness / Component | Hardware Safety Function |
|---|---|---|---|
| **GPIO16** | Pin 4 (`R` / `RXD`) | - | Passive TWAI receiver input (500 kbps) |
| **DISCONNECTED** | Pin 1 (`D` / `TXD`) | Wire to 3.3V via 4.7kΩ–10kΩ pull-up | **PHYSICAL TX ISOLATION**: Driver held permanently recessive |
| **GND** | Pin 8 (`Rs`) | - | Standard High-Speed Mode ($\le 50\text{ns}$ propagation delay) |
| **3.3V** | Pin 3 (`VCC`) | - | 3.3V transceiver power supply |
| **GND** | Pin 2 (`GND`) | Motorcycle diagnostic ground | Common ground reference |
| - | Pin 7 (`CAN_H`) | Motorcycle OBD Pin 6 / OEM CAN_H | Differential bus high (120Ω breakout resistor removed) |
| - | Pin 6 (`CAN_L`) | Motorcycle OBD Pin 14 / OEM CAN_L | Differential bus low (120Ω breakout resistor removed) |
| **GPIO27** | - | WS2812B DIN via 330Ω | Shift light data output |
| **VIN (5V)** | - | Motorcycle 5V buck converter | Regulated 5V ESP32 power supply |

---

## 3. Building & Uploading

### Native Windows Build (`build.ps1`)

PulseShift includes a native Windows build script supporting all four firmware configurations:

```powershell
Set-Location "D:\Development\PulseShift-Firmware"

# Build options: demo (default), capture, vehicle, wokwi, or all
.\build.ps1 -Target capture
```

### Exact Upload Command (Arduino CLI)

Identify your ESP32 COM port and flash the pre-compiled `capture` binary:

```powershell
& "D:\Tools\ArduinoCLI-1.5.1\arduino-cli.exe" --config-file "D:\Arduino\arduino-cli.yaml" upload -p COMx --fqbn esp32:esp32:esp32:FlashMode=dio,FlashFreq=40,UploadSpeed=115200 --input-dir "D:\Development\PulseShift-Firmware\build\capture" "D:\Development\PulseShift-Firmware\PulseShift"
```

*(Replace `COMx` with your actual serial port, e.g. `COM5`).*

### Opening in Arduino IDE 2.x

Run the helper launcher or open `PulseShift\PulseShift.ino`:

```powershell
.\Open-Arduino.ps1
```

In Arduino IDE:
1. Select **ESP32 Dev Module**.
2. Flash Frequency: **40MHz**, Flash Mode: **DIO**, Upload Speed: **115200**.
3. Partition Scheme: **Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)**.

---

## 4. Offline Motorcycle Recording Procedure

When testing on your motorcycle with no computer, PulseShift runs entirely autonomously.

### Field Capture Sequence

1. **Power the ESP32:**
   - Connect the regulated 5V supply to `VIN` and `GND`.
   - The 6 WS2812B LEDs execute a 1.2-second forward/reverse sweep confirming power and LED operation.
2. **Observe LED 0 (Waiting for Bus):**
   - **Pulsing Cyan (slow 1Hz breath):** Firmware initialized, LittleFS mounted, waiting for motorcycle CAN bus activity.
3. **Turn Motorcycle Ignition Key ON (Engine OFF):**
   - As soon as the ECU begins broadcasting:
     - **LED 0 turns solid Cyan:** CAN traffic actively detected.
     - **LED 1 blinks Green:** Heartbeat indicator for incoming CAN frames.
     - **LED 2 turns solid Yellow:** Stage 1 active (**Key ON / Engine OFF**).
   - Record for **30 to 45 seconds** to capture stationary baseline frames.
4. **Transition to Stage 2 (Engine Idling):**
   - Press the ESP32 **BOOT** button (GPIO0, short press < 1.5s):
     - **All 6 LEDs flash White for 200ms** (tactile confirmation).
     - **LEDs 2 & 3 turn solid Yellow:** Stage 2 active (**Engine Idling**).
   - Start the engine. Let it idle steadily for **45 to 60 seconds** (~1400 RPM).
5. **Transition to Stage 3 (Gentle Stationary Revving):**
   - Press the **BOOT** button again:
     - **All 6 LEDs flash White for 200ms**.
     - **LEDs 2, 3 & 4 turn solid Yellow:** Stage 3 active (**Gentle RPM Sweeps**).
   - In neutral, gently twist the throttle to hold steady RPM increments (e.g. 2500, 3500, 4500 RPM for 5 seconds each).
6. **Finish Capture:**
   - Turn off the motorcycle ignition key.
   - Disconnect power from the ESP32.
   - All captured frames and diagnostic events are safely committed to LittleFS SPI flash.

---

### LED Status Reference

| LED Index | Visual Pattern | Operating State |
|---|---|---|
| **LED 0** | Slow Cyan Pulse | Initialized, waiting for CAN bus frames |
| **LED 0** | Solid Cyan | Active CAN traffic detected |
| **LED 0** | Rapid Red Flash | Fatal bus-off / driver fault |
| **LED 1** | Green Blinks | CAN frame reception heartbeat |
| **LED 2** | Solid Yellow | Stage 1 Active: Key-On / Engine-Off |
| **LEDs 2, 3** | Solid Yellow | Stage 2 Active: Engine Idling |
| **LEDs 2, 3, 4** | Solid Yellow | Stage 3 Active: Gentle Stationary RPM |
| **All LEDs** | Momentary White Flash (200ms) | Button press confirmed (stage transition) |
| **LED 5** | Off | Storage healthy |
| **LED 5** | Slow Amber Blink | Storage Full (quota reached; logging safely stopped) |
| **LED 5** | Magenta Blink | Storage mount or filesystem error |

---

## 5. Storage Capacity, Duration & Power Loss Behavior

### Flash Storage Budget
- **LittleFS Partition Total:** 1,441,792 bytes (~1.4 MB).
- **Safety Headroom Threshold:** 32,768 bytes (32 KB minimum free space).
- **Diagnostic Event Log Limit:** 131,072 bytes (128 KB bounded circular `/diag.log`).
- **Raw CAN Capture Limit (`PULSESHIFT_MAX_CAPTURE_BYTES`):** 1,000,000 bytes (1 MB `/can.raw`).
- **Binary Record Structure:** 76-byte `CaptureHeader` + 20 bytes per frame (`CanLogRecord`).
- **Maximum Frame Capacity:** $\lfloor(1{,}000{,}000 - 76) / 20\rfloor = 49{,}996\text{ frames}$.

### Recording Duration by Bus Load

| Aggregate Bus Rate | Data Throughput | 49,996 Records Capacity | Motorcycle Application |
|---|---|---|---|
| **50 frames/sec** | 1.0 KB/s | **16.6 minutes** (1,000s) | Quiet bus / idle ECU broadcasts |
| **100 frames/sec** | 2.0 KB/s | **8.3 minutes** (500s) | Baseline cruising broadcast rate |
| **150 frames/sec** | 3.0 KB/s | **5.5 minutes** (333s) | **Recommended test rate** (ECU + ABS active) |
| **200 frames/sec** | 4.0 KB/s | **4.1 minutes** (250s) | Multi-node active network |
| **300 frames/sec** | 6.0 KB/s | **2.7 minutes** (166s) | Dense bus with high-frequency telemetry |

### Power-Loss Durability Guarantees
- **Committed Data:** The in-memory buffer flushes to LittleFS when reaching **8 records (160 bytes)**, when **200 ms** elapses, or upon **stage transition**. All committed flash blocks are 100% durable across power loss.
- **In-Flight Data:** During sudden power cuts (e.g. key switch turned off), only unwritten frames currently in volatile RAM from the active 200 ms window (at most 8 records) are lost.
- **Torn Write Recovery:** If power collapses during an SPI flash block write, LittleFS's copy-on-write design prevents filesystem corruption. Any partial trailing record (<20 bytes) is flagged and discarded by the export tool's CRC16 validation, preserving all prior records intact.
- **First-Boot Storage Safety:** On initial boot, the firmware checks if the partition is virgin flash (all `0xFF`). If blank, it formats LittleFS automatically. If existing data is detected, automatic formatting is suppressed to protect saved recordings.

---

## 6. Retrieving Saved Logs on Windows

When you return home, plug the ESP32 into your Windows PC via USB:

### One-Command PowerShell Retrieval (Recommended)

```powershell
Set-Location "D:\Development\PulseShift-Firmware"
powershell -ExecutionPolicy Bypass -File .\export-logs.ps1
```

The script automatically:
1. Detects your ESP32 serial COM port.
2. Reads system status, boot count, and CAN statistics -> `status.txt` & `metadata.json`.
3. Downloads the persistent diagnostic event log -> `diagnostics.log` & `diagnostics.json`.
4. Downloads all raw CAN frames, evaluates CRC16 checksums, and exports -> `can_capture.csv` & `can_capture.json`.
5. Saves all outputs into a timestamped directory: `.\logs\export_YYYYMMDD_HHMMSS\`.

---

## 7. Automated Testing & Verification

PulseShift features an automated test harness covering compilation, symbol isolation, static safety checks, and functional integration:

```powershell
Set-Location "D:\Development\PulseShift-Firmware"
powershell -ExecutionPolicy Bypass -File .\test-firmware.ps1
```

### Test Coverage (22 of 22 Suites Passed)
- **Suite 1:** Native compilation of all 4 targets (`demo`, `wokwi`, `capture`, `vehicle`).
- **Suite 2:** Linker symbol isolation verification (`xtensa-esp32-elf-nm.exe` confirms zero forbidden transmission symbols).
- **Suite 3:** CRC16-CCITT bitflip and corruption detection.
- **Suite 4:** Storage quota calculations, bounds, and first-boot virgin flash formatting safety.
- **Suite 5:** BOOT button 50ms contact debounce, short-press progression, and long-press toggle FSM.
- **Suite 6:** RPM fail-closed decoding, counter continuity, and stale data invalidation.
- **Suite 7:** Startup animation independence regardless of initial bus state.
- **Suite 8:** PowerShell and Python export tool syntax and parameter validation.
- **Suite 9:** 17-case Python functional integration suite (`test_offline_diagnostics.py`).

---

## 8. Identifying Motorcycle RPM from Exported Logs

Open `can_capture.csv` in Excel or Python. The `stage` column isolates your test phases:

```csv
timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok
15200,00000120,0,0,8,0000000000000000,1,1
...
45300,00000120,0,0,8,0578000000000000,2,1
...
78400,00000120,0,0,8,0DAC000000000000,3,1
```

1. **Stage 1 (Key ON, Engine OFF):** RPM bytes are `0` or constant.
2. **Stage 2 (Idling):** RPM bytes reflect idle speed (~1400 RPM -> `0x0578` at 1 RPM/LSB or `0x0AE0` at 0.5 RPM/LSB).
3. **Stage 3 (Revving):** RPM bytes increase proportionally with engine speed (e.g. 3500 RPM -> `0x0DAC`).

Once you identify the arbitration ID and byte offset, configure `vehicle_profile.cpp`, build with `.\build.ps1 -Target vehicle`, and PulseShift is ready for live motorcycle operation!

---

## 9. License

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
