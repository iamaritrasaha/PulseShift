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

## 3. Physical Motorcycle Wiring & Electrical Safety

Connect the ESP32 and SN65HVD230 CAN transceiver to the motorcycle harness:

1. **Power Supply:**
   - Connect +5V from a stable DC-DC buck converter (or motorcycle USB accessory port) to ESP32 **VIN / 5V** and **GND**.
   - Power the SN65HVD230 transceiver from ESP32 **3.3V** and **GND** (SN65HVD230 is a true 3.3V transceiver).
2. **CAN Bus Connections:**
   - **CAN_H** -> Motorcycle diagnostic connector CAN High (OBD pin 6 or OEM diagnostic port).
   - **CAN_L** -> Motorcycle diagnostic connector CAN Low (OBD pin 14 or OEM diagnostic port).
   - **Transceiver Termination:** Motorcycle harness is already terminated (60Ω equivalent). Ensure the 120Ω termination resistor on the transceiver breakout is removed if tapping into an already-terminated live bus.
3. **Controller I/O Pins:**
   - **GPIO16 (RX):** Connected to Transceiver Pin 4 (`R` / `RXD`).
   - **GPIO17 (TX):** Transceiver Pin 1 (`D` / `TXD`). The firmware automatically isolates this pin as a high-Z input with internal pull-up to maintain strictly passive listen-only behavior.
   - **Transceiver `Rs` (Pin 8):** For complete hardware transmission lockout, wire `Rs` to 3.3V (forces listen-only silent mode).
4. **WS2812B LEDs:**
   - Data In (DIN) -> ESP32 **GPIO27** through a 330Ω series resistor.

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

## 6. How Errors and Data Survive Power Disconnection

1. **Non-Destructive LittleFS Storage:** Uses the standard 1.375 MB data partition (`spiffs`) on the 4 MB flash. Firmware never automatically formats or erases data.
2. **Frequent Flush & Wear Mitigation:** Raw frames are queued in a 2560-byte in-memory ring buffer and written to flash in batch blocks (every 16 frames or 500ms). This decouples real-time CAN reception from flash SPI write delays.
3. **Reset Reason Tracking:** On boot, `esp_reset_reason()` captures whether the reset was caused by normal power-on, brownout (voltage dip below 2.7V), watchdog timeout, or software exception, and writes it directly to `/diag.log`.
4. **Power Loss Boundary:** In the event of abrupt battery/power disconnection, at most the last ~500ms of in-flight RAM buffer is lost; all previously flushed records remain intact and validated by 16-bit CRC.

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
