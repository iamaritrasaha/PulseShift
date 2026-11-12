# PulseShift — Windows 11 Development, Offline Motorcycle CAN Diagnostics & Capture

This guide covers building, uploading, field-testing, and retrieving offline diagnostics and raw CAN capture data from PulseShift on ESP32 DevKit V1 (ESP32-WROOM-32) with native Windows 11 tools (Arduino IDE 2.3.10 and Arduino CLI 1.5.1).

---

## 1. Quick Start & Build Verification

Open PowerShell 7 (or Windows PowerShell) and run:

```powershell
Set-Location D:\Development\PulseShift-Firmware
.\build.ps1 -Target all
.\test-firmware.ps1
```

### Build Targets

| Target | Description | Operating Mode | TWAI Driver |
|---|---|---|---|
| `demo` | Standalone LED shift-light simulation with 6 WS2812B LEDs. No CAN required. | `PULSESHIFT_MODE=1` | Disabled (0 TWAI symbols) |
| `wokwi` | Same demo with 128x64 I2C OLED enabled for simulator. | `PULSESHIFT_MODE=1` | Disabled |
| `capture` | **Offline Passive CAN Capture & Diagnostics for motorcycle testing.** | `PULSESHIFT_MODE=3` | Active listen-only (TX isolated) |
| `vehicle` | Qualified vehicle profile listener (fail-closed unverified NS125 profile). | `PULSESHIFT_MODE=4` | Active listen-only |

Build outputs and caches stay in `build/<target>` and `cache/<target>` on D:.

---

## 2. Preparing Firmware for Motorcycle Testing

When heading out to test on your **Bajaj Pulsar NS125 2025 ABS**, you will **NOT** have a computer or laptop. The firmware runs autonomously, recording events, errors, and raw CAN traffic directly to internal LittleFS flash storage.

### Step 1: Configure Bitrate in `UserConfig.h`

Open `PulseShift/UserConfig.h` and configure the 500 kbit/s candidate setting:

```c
#define PULSESHIFT_CAN_BITRATE 500000
```

*(Note: Leaving bitrate as `0` keeps the CAN controller stopped for safety until you set a bitrate).*

### Step 2: Build the `capture` Binary

```powershell
Set-Location D:\Development\PulseShift-Firmware
.\build.ps1 -Target capture
```

### Step 3: Upload to ESP32 over USB

Identify your ESP32 COM port and upload:

```powershell
$cli = 'D:\Tools\ArduinoCLI-1.5.1\arduino-cli.exe'
$cfg = 'D:\Arduino\arduino-cli.yaml'

# Find port
& $cli --config-file $cfg board list

# Replace COMx with your port (e.g. COM3):
& $cli --config-file $cfg upload --fqbn esp32:esp32:esp32:FlashMode=dio,FlashFreq=40,UploadSpeed=115200 --port COMx --input-dir .\build\capture .\PulseShift
```

---

## 3. Physical Motorcycle Wiring & Hardware-Enforced Passive Safety

To guarantee that PulseShift can **NEVER** interfere with the motorcycle's ECU, ABS, or electronic fuel injection, the hardware setup uses a **dual-layer hardware-enforced passive listening architecture**:

### Critical Safety Invariants (SN65HVD230 Transceiver)

1. **GPIO17 (TXD) Physical Isolation & Passive Recessive Holding:**
   - **DO NOT connect ESP32 GPIO17 to Transceiver Pin 1 (D / TXD).**
   - Leave ESP32 GPIO17 **physically disconnected** (no wire, no jumper, no breadboard trace).
   - Tie Transceiver Pin 1 (`D`) directly to **3.3V (`VCC`)** or through an external 4.7kΩ to 10kΩ pull-up resistor.
   - *Datasheet Verification:* Per Section 8.4.1 and Table 1 (Driver Function Table) of the TI SN65HVD230 datasheet, when input $D = \text{HIGH}$ ($V_{IH} \ge 2\text{ V}$), the differential driver is forced into the **RECESSIVE** state ($V_{OD(R)} \approx 0\text{ V}$, $V_{CANH} \approx V_{CANL} \approx 2.3\text{ V}$). The driver CANNOT physically drive a dominant bit on the bus, regardless of MCU state or firmware panics.
   - The firmware additionally configures GPIO17 as a High-Z input with internal pull-up as a redundant software guard.

2. **Transceiver High-Speed Mode for 500 kbps (Pin 8 `Rs` tied to GND):**
   - Connect Transceiver Pin 8 (`Rs`) to **GND** ($V_{Rs} < 1.2\text{ V}$, $R_s = 0\Omega$).
   - *Datasheet Reconciled Mode Analysis:*
     - Applying $V_{Rs} > 0.75 V_{CC}$ (Pin 8 HIGH) places the SN65HVD230 into **Low-Power Standby Mode**. In standby, while the driver is shut off, the internal receiver circuitry is switched to a reduced-bias state with increased propagation delay and pulse skew. Operating in standby mode can compromise continuous, error-free decoding of 500 kbps CAN traffic under noisy vehicle operating conditions.
     - Applying $V_{Rs} < 1.2\text{ V}$ (Pin 8 LOW / GND) selects **Normal High-Speed Mode**, providing full receiver bandwidth, minimal propagation delay ($t_{PLH}, t_{PHL} \le 50\text{ ns}$), and pulse skew $\le 10\text{ ns}$, perfectly meeting the 2000 ns bit-timing requirements of 500 kbps CAN.
     - *Breakout Board Compatibility:* Common blue SN65HVD230 breakout boards already have Pin 8 (`Rs`) tied to GND via an onboard 0Ω SMD resistor or copper trace. **No trace cutting or desoldering of Pin 8 is required.**
     - *Transmission Prevention Guarantee:* Because Pin 1 (`D`) is permanently held HIGH and GPIO17 is physically disconnected, the driver remains 100% passive and recessive at all times.

3. **Onboard 120Ω Termination Resistor Removal (MANDATORY):**
   - An active motorcycle CAN network is **already terminated** with two 120Ω resistors (one in the ECU, one in the instrument cluster), yielding an equivalent resistance of 60Ω across CAN_H and CAN_L.
   - Most SN65HVD230 breakout boards include an onboard 120Ω SMD resistor (often labeled `R2` or `120`).
   - If connected without modification, the three parallel resistors reduce bus resistance to $40\Omega$ ($120 \parallel 120 \parallel 120 = 40\Omega$), overloading vehicle transceivers and corrupting ECU communication.
   - **You MUST remove/desolder the onboard 120Ω resistor on the breakout board** before connecting to the motorcycle harness. Measure with a multimeter across CAN_H and CAN_L when disconnected: it should read open circuit (>100kΩ).

4. **Reliable 500 kbps Reception Wiring:**
   - Wire Transceiver Pin 4 (`R` / `RXD`) to ESP32 **GPIO16**.
   - Wire Transceiver `GND` to ESP32 `GND` and motorcycle harness signal ground.
   - Wire Transceiver `VCC` to ESP32 **3.3V** (do NOT power from 5V).

### Wiring Pinout Summary

| ESP32 Pin | Transceiver Pin | Harness / Component | Hardware Safety Function |
|---|---|---|---|
| **GPIO16** | Pin 4 (`R` / `RXD`) | - | Passive TWAI receiver input (500 kbps) |
| **DISCONNECTED** | Pin 1 (`D` / `TXD`) | Wire to 3.3V via 4.7kΩ–10kΩ pull-up | **PHYSICAL TX ISOLATION**: Driver held permanently recessive |
| **GND** | Pin 8 (`Rs`) | - | Standard High-Speed Mode ($\le 50\text{ns}$ propagation delay) |
| **3.3V** | Pin 3 (`VCC`) | - | 3.3V transceiver supply |
| **GND** | Pin 2 (`GND`) | Motorcycle diagnostic ground | Common ground reference |
| - | Pin 7 (`CAN_H`) | Motorcycle OBD Pin 6 / OEM CAN_H | Differential bus high (120Ω breakout resistor removed) |
| - | Pin 6 (`CAN_L`) | Motorcycle OBD Pin 14 / OEM CAN_L | Differential bus low (120Ω breakout resistor removed) |
| **GPIO27** | - | WS2812B DIN via 330Ω | Shift light data output |
| **VIN (5V)** | - | Motorcycle 5V buck converter | Regulated 5V ESP32 power supply |

---

## 4. Offline Motorcycle Recording Procedure

Once at your motorcycle with no laptop:

### Sequence of Operation

1. **Power the ESP32:**
   - The 6 WS2812B LEDs execute a 1.2-second startup sweep (forward sweep, reverse sweep, confirmation pulse) verifying power and LED operation.
2. **Observe LED 0 (Waiting for Bus):**
   - **Pulsing Cyan (slow 1Hz breath):** Firmware initialized, LittleFS mounted, waiting for motorcycle CAN bus activity.
   - If LED 0 flashes rapid Red: CAN driver fault or bus-off.
   - If LED 5 flashes Magenta: Storage error.
3. **Turn Motorcycle Ignition Key ON (Engine OFF):**
   - As soon as the ECU powers on and begins broadcasting:
     - **LED 0 turns solid Cyan:** CAN traffic actively detected.
     - **LED 1 blinks Green:** Heartbeat indicator for each batch of incoming CAN frames.
     - **LED 2 turns solid Yellow:** Stage 1 active (**Key ON / Engine OFF**).
   - Let it record for 15–30 seconds in Stage 1 to capture stationary baseline frames.
4. **Transition to Stage 2 (Engine Idling):**
   - Press the ESP32 **BOOT** button (GPIO0, short press ~0.2s):
     - **All 6 LEDs flash White for 200ms** (positive tactile feedback).
     - **LEDs 2 & 3 turn solid Yellow:** Stage 2 active (**Engine Idling**).
   - Start the engine with the starter button. Let the engine idle steadily for 30–60 seconds.
5. **Transition to Stage 3 (Gentle Stationary Revving):**
   - Press the **BOOT** button again (short press):
     - **All 6 LEDs flash White for 200ms**.
     - **LEDs 2, 3 & 4 turn solid Yellow:** Stage 3 active (**Gentle RPM Sweeps**).
   - Gently twist the throttle to hold steady RPM increments (e.g. 2500 RPM for 5s, 3500 RPM for 5s, 4500 RPM for 5s).
6. **Finish Capture:**
   - Turn off the motorcycle ignition key.
   - Unplug power from the ESP32.
   - All captured frames and diagnostic events are safely stored in LittleFS internal flash!

### Summary of LED Status Indications

| LED Index | Visual Pattern | Meaning |
|---|---|---|
| **LED 0** | Slow Cyan Pulse | Initialized, waiting for CAN bus frames |
| **LED 0** | Solid Cyan | CAN traffic active |
| **LED 0** | Rapid Red Flash | Fatal bus-off / driver fault |
| **LED 1** | Green Blinks | CAN frame reception heartbeat |
| **LED 2** | Solid Yellow | Stage 1 Active: Key-On / Engine-Off |
| **LEDs 2, 3** | Solid Yellow | Stage 2 Active: Engine Idling |
| **LEDs 2, 3, 4** | Solid Yellow | Stage 3 Active: Gentle Stationary RPM |
| **All LEDs** | Momentary White Flash (200ms) | Button press registered (stage changed) |
| **LED 5** | Off | Storage healthy |
| **LED 5** | Slow Amber Blink | Storage Full (limit reached; safely stopped) |
| **LED 5** | Magenta Blink | Storage mount or filesystem error |

---

## 5. Retrieving Saved Logs on Windows

When you return home, plug the ESP32 into your Windows 11 PC via USB:

### Automated PowerShell Retrieval (Recommended)

Run:

```powershell
Set-Location D:\Development\PulseShift-Firmware
.\export-logs.ps1
```

The script automatically:
1. Detects your ESP32 COM port.
2. Opens connection at 115200 baud without resetting the microcontroller.
3. Retrieves system status, storage metrics, boot reason, and CAN controller telemetry -> `status.txt` & `metadata.json`.
4. Downloads the persistent diagnostic event log -> `diagnostics.log` & `diagnostics.json`.
5. Downloads all recorded raw CAN frames with per-record CRC16 validation -> `can_capture.csv` & `can_capture.json`.
6. Saves everything in a timestamped folder: `.\logs\export_YYYYMMDD_HHMMSS\`.
7. Displays a summary report verifying 100% transfer integrity.

To clear ESP32 memory after export:
```powershell
.\export-logs.ps1 -ClearAfterExport
```

### Python Alternative

```powershell
python .\export_logs.py
```

### Manual Serial Monitor Commands

If using Arduino IDE Serial Monitor (115200 baud, CRLF):
- `STATUS`: Displays uptime, boot count, reset reason, storage usage, CAN statistics, and RPM state.
- `LOGS`: Dumps persistent diagnostic events from `/diag.log`.
- `DUMP`: Dumps all raw CAN frames in CSV format with CRC checksums.
- `CLEAR`: Prompts for confirmation.
- `CLEAR YES`: Permanently erases stored offline logs and resets session.
- `STAGE <1|2|3>`: Manually switches active capture stage.
- `HELP`: Displays command menu.

---

## 6. Power Loss, Flushing & Realistic Data Recovery Guarantees

Capturing CAN traffic on a motorcycle means the ESP32 can experience abrupt power loss when the ignition key is switched off, or if battery voltage drops during engine cranking. The firmware provides explicit, non-destructive recovery guarantees:

### Flushing Architecture & Sync Cadence
1. **Multi-Condition Flushing:** The in-memory queue automatically flushes to LittleFS when:
   - The buffer reaches **8 records (160 bytes)** (approx. 40–80 ms under normal 100–200 fps traffic).
   - Or **200 ms** elapses since the last write (ensuring slow 1–10 fps baseline traffic is committed quickly).
   - Or when a **stage transition** is commanded via the BOOT button (flushes all preceding stage frames before advancing).
2. **Explicit Filesystem Synchronization:** Each flush executes `f.write()`, `f.flush()`, and `f.close()`, which forces `lfs_file_sync()` and commits directory index nodes to SPI flash.

### Realistic Power-Loss Recovery Guarantees
- **Durable Data:** All records committed in preceding flush cycles are 100% durable in LittleFS SPI flash and cannot be lost.
- **In-Flight Data at Power Cut:** Only the frames currently sitting in the volatile RAM ring buffer that have not yet been flushed (at most 8 to 40 frames, or frames accumulated within the last 200 ms window) are lost when power is abruptly removed.
- **Torn Record Protection:** If DC voltage collapses *during* an active SPI flash block write, LittleFS's atomic copy-on-write design prevents filesystem corruption by rolling back the incomplete transaction. If a trailing partial record (fewer than 20 bytes) remains on flash, the retrieval tool validates record boundaries and evaluates the CCITT CRC16 checksum on every frame. Any incomplete or bit-corrupted trailing record is cleanly flagged and rejected, preserving all previously written frames.
- **Reset Cause Forensic Logging:** When power is restored, `esp_reset_reason()` inspects hardware RTC registers. If the reboot was caused by an automotive power-rail dip, `BROWNOUT` is logged; if caused by software exception, `EXCEPTION_PANIC` is logged; if caused by normal key-on, `POWERON` is logged. All reset reasons are permanently recorded in `/diag.log`.

---

## 7. Storage Capacity, Duration & Field Constraints

### Storage Budget
- **LittleFS Partition Total:** 1,441,792 bytes (~1.4 MB).
- **Safety Headroom Threshold:** 32,768 bytes (32 KB minimum free space).
- **Diagnostic Event Log Limit:** 131,072 bytes (128 KB bounded circular `/diag.log`).
- **Raw CAN Capture File Limit (`PULSESHIFT_MAX_CAPTURE_BYTES`):** 1,000,000 bytes (1 MB `/can.raw`).
- **Binary Record Framing:**
  - Header: 76 bytes (`CaptureHeader`).
  - Records: 20 bytes per frame (`CanLogRecord`).
  - **Maximum Record Capacity:** $\lfloor(1{,}000{,}000 - 76) / 20\rfloor = 49{,}996\text{ frames}$.

### Realistic Recording Duration by Bus Load

The duration that 1 MB of storage lasts depends directly on the motorcycle's aggregate CAN transmission rate:

| Aggregate Bus Rate | Data Throughput | 49,996 Records Capacity | Practical Application on Motorcycle |
|---|---|---|---|
| **50 frames/sec** | 1.0 KB/s | **16.6 minutes** (1,000s) | Single-cylinder ECU with minimal CAN broadcasting |
| **100 frames/sec** | 2.0 KB/s | **8.3 minutes** (500s) | Typical cruising / baseline broadcast rate |
| **150 frames/sec** | 3.0 KB/s | **5.5 minutes** (333s) | **Recommended test session rate** (ECU + ABS active) |
| **200 frames/sec** | 4.0 KB/s | **4.1 minutes** (250s) | High broadcast frequency (ECU, ABS, Cluster, IMU) |
| **300 frames/sec** | 6.0 KB/s | **2.7 minutes** (166s) | Busy multi-node vehicle network |
| **500 frames/sec** | 10.0 KB/s | **1.6 minutes** (100s) | Heavy bus load (~25% bus utilization @ 500 kbps) |

### Capture Session Protocol & Time Budget
To guarantee that all 3 capture stages fit comfortably within the 1 MB storage bound without truncation:
1. **Stage 1 (Key ON / Engine OFF):** Record for **30 to 45 seconds** (captures stationary baselines).
2. **Stage 2 (Engine Idling):** Record for **45 to 60 seconds** (captures stable idle RPM ~1400 RPM).
3. **Stage 3 (Stationary Revving):** Record for **45 to 60 seconds** (gentle sweeps up to 4000–6000 RPM in neutral).
- **Total Recommended Session:** **2.5 to 3 minutes** (~25,000 frames @ 150 fps, utilizing ~500 KB / 50% capacity).
- **Storage-Full Safeguard:** If recording exceeds the 1 MB bound, the firmware asserts `storageFull = true`, turns **LED 5 steady Amber**, and cleanly halts logging without crashing or corrupting earlier records.

---

## 8. Identifying Motorcycle RPM from Exported Logs

Once exported to `can_capture.csv`, open the file in Excel or Python. The `stage` column isolates your test phases:

```csv
timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok
15200,00000120,0,0,8,0000000000000000,1,1
...
45300,00000120,0,0,8,0578000000000000,2,1
...
78400,00000120,0,0,8,0DAC000000000000,3,1
```

- In Stage 1 (Key ON, Engine OFF): RPM payload bytes will be `0` (or constant).
- In Stage 2 (Idling): RPM payload bytes will reflect idle speed (~1400 RPM -> `0x0578` at 1 RPM/LSB or `0x0AE0` at 0.5 RPM/LSB).
- In Stage 3 (Revving): RPM payload bytes will increase proportionally (e.g. 3500 RPM -> `0x0DAC`).

Filter by arbitration ID to locate the matching signal, calibrate `scaleNumerator` and `scaleDenominator` in `vehicle_profile.cpp`, and PulseShift's progressive shift-light is ready for live motorcycle operation!
