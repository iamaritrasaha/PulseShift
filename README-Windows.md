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

1. **GPIO17 (TXD) Physical Isolation:**
   - **DO NOT connect ESP32 GPIO17 to Transceiver Pin 1 (D / TXD).**
   - Transceiver Pin 1 (`D`) must be **physically isolated from GPIO17** and tied to **3.3V (`VCC`)** through an external 4.7kΩ to 10kΩ pull-up resistor (or tied directly to 3.3V).
   - Per the TI SN65HVD230 datasheet, holding `D` permanently HIGH forces the driver into the recessive (passive) state, physically preventing dominant bus driving even if the controller malfunctions.
   - ESP32 GPIO17 is left completely unconnected. The firmware additionally configures GPIO17 as a High-Z input with internal pull-up as a secondary software safeguard.
2. **Transceiver Silent / Listen-Only Mode (Pin 8 `Rs`):**
   - Transceiver Pin 8 (`Rs`) must be wired directly to **3.3V (`VCC`)** ($V_{Rs} \ge 0.75 V_{CC}$).
   - Per Section 8.4.2 of the TI SN65HVD230 datasheet, when $V_{Rs} \ge 0.75 V_{CC}$, the transceiver's differential transmitter driver is completely disabled, while the differential receiver remains 100% active.
   - **Breakout Board Warning:** Common blue SN65HVD230 breakout boards tie `Rs` to GND via a 0Ω surface-mount resistor (placing it in High-Speed transmit mode). You **MUST desolder or cut the trace** to GND, and wire `Rs` to 3.3V.
3. **Onboard 120Ω Termination Resistor Removal:**
   - An active motorcycle CAN network is **already terminated** with two 120Ω resistors (one in the ECU, one in the instrument cluster), yielding an equivalent resistance of 60Ω across CAN_H and CAN_L.
   - Most SN65HVD230 breakout boards include an onboard 120Ω SMD resistor (often labeled `R2` or `120`).
   - If connected without modification, the three parallel resistors reduce bus resistance to $40\Omega$ ($120 \parallel 120 \parallel 120 = 40\Omega$), overloading vehicle transceivers and corrupting ECU communication.
   - **You MUST remove/desolder the onboard 120Ω resistor on the breakout board** before connecting to the motorcycle harness. Measure with a multimeter across CAN_H and CAN_L when disconnected: it should read open circuit (>100kΩ).
4. **Reliable 500 kbps Reception:**
   - The SN65HVD230 receiver supports signaling rates up to 1 Mbps with receiver propagation delay $\le 50\text{ns}$, well within the 2000 ns bit budget of 500 kbps CAN.
   - Wire Transceiver Pin 4 (`R` / `RXD`) to ESP32 **GPIO16**.
   - Wire Transceiver `GND` to ESP32 `GND` and motorcycle harness signal ground.
   - Wire Transceiver `VCC` to ESP32 **3.3V** (do NOT power from 5V).

### Wiring Pinout Summary

| ESP32 Pin | Transceiver Pin | Harness / Component | Hardware Safety Function |
|---|---|---|---|
| **GPIO16** | Pin 4 (`R` / `RXD`) | - | Passive TWAI receiver input |
| **NO CONNECTION** | Pin 1 (`D` / `TXD`) | Wire to 3.3V via 4.7kΩ pull-up | **PHYSICAL TX ISOLATION**: Transceiver driver held permanently recessive |
| **3.3V** | Pin 3 (`VCC`) & Pin 8 (`Rs`) | - | Supplies 3.3V; forces transceiver into Listen-Only / Silent mode |
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

## 7. Storage Capacity & Constraints

- **Storage Ceiling:** 1,000,000 bytes (~1 MB) allocated for raw CAN capture, and 128 KB for diagnostic logs, leaving 250+ KB free safety headroom.
- **Frame Capacity:** Each packed binary CAN record occupies 20 bytes (including timestamp, ID, DLC, stage, 8 payload bytes, and CRC16). The file holds up to **50,000 frames** (approx. 8–10 minutes of continuous 100 fps traffic).
- **Storage-Full Policy:** When storage reaches 1 MB or free space drops below 32 KB, the firmware sets `storageFull = true`, logs the event, flashes LED 5 Amber, and gracefully stops logging without crashing or corrupting earlier records.

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
