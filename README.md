# PulseShift

Customizable ESP32 motorcycle shift-light firmware. Six WS2812B LEDs light up progressively with engine RPM. At 6000 RPM all LEDs flash red as the shift alert. No motorcycle is needed to try it: the default Demo mode runs a full RPM simulation on its own.

## What it does

- **Demo mode**: runs an autonomous synthetic RPM sweep from 0 to 6000 RPM and back, showing the shift light in action. No CAN connection needed.
- **CAN Capture mode**: passively records raw CAN bus frames from the motorcycle to internal flash (LittleFS), for identifying the RPM message.
- **Vehicle RPM mode**: live shift light driven by decoded motorcycle CAN data (requires a verified vehicle profile, currently unavailable for the NS125).
- **Automatic error logging**: CAN faults, storage errors, and reset reasons are saved to flash and readable from Serial Monitor.

## Parts

| Part | Notes |
|---|---|
| ESP32 DevKit V1 (ESP32-WROOM-32) | With USB data cable |
| 6× WS2812B LED strip (5V, GRB, 800kHz) | 6-LED segment |
| 330Ω resistor | In series with LED data line |
| 1000µF 10V electrolytic capacitor | Across LED power rails |
| Regulated 5V supply, ≥ 1A | For LEDs only |
| SN65HVD230 3.3V CAN transceiver | For CAN modes only |

## Arduino IDE: Board Settings

| Setting | Value |
|---|---|
| Board | **ESP32 Dev Module** |
| Upload Speed | **921600** |
| Flash Size | **4MB (32Mb)** |
| Partition Scheme | **Default 4MB with spiffs** |
| PSRAM | **Disabled** |

> [!IMPORTANT]
> The "Default 4MB with spiffs" partition scheme includes a LittleFS-compatible data partition used for automatic diagnostic logging and CAN capture. PulseShift formats this partition on first boot if it is blank. It never erases an existing partition that already contains data.

### Required libraries (install via Library Manager)

- **Adafruit NeoPixel** ≥ 1.12.3
- **Adafruit SSD1306** ≥ 2.5.13 *(only needed if OLED is enabled)*
- **Adafruit GFX Library** ≥ 1.11.11 *(only needed if OLED is enabled)*
- **Adafruit BusIO** ≥ 1.17.0 *(only needed if OLED is enabled)*

### ESP32 board support

Add this URL to **File → Preferences → Additional board manager URLs**:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then install **esp32 by Espressif Systems** (version 2.0.17 recommended).

## How to upload

1. Open `PulseShift/PulseShift.ino` in Arduino IDE.
2. Select **ESP32 Dev Module** in Tools → Board.
3. Set partition scheme to **Default 4MB with spiffs**.
4. Select your COM port.
5. Click **Upload** (Ctrl+U).

That's it. No scripts, no command line.

## LED behavior: 0 to 6000 RPM

| RPM | LEDs |
|---|---|
| 0 | All off |
| 1-1000 | LED 1 (green) |
| 1001-2000 | LED 1-2 (green) |
| 2001-3000 | LED 1-3 (green) |
| 3001-4000 | LED 1-4 (green/yellow) |
| 4001-5000 | LED 1-5 (yellow/orange) |
| 5001-5999 | LED 1-6 (orange/red) |
| >= 6000 | All LEDs flash red: **shift!** |

Thresholds are in `UserConfig.h` as `PULSESHIFT_DEMO_START_RPM`, `PULSESHIFT_DEMO_SHIFT_RPM`, and `PULSESHIFT_DEMO_MAX_RPM`.

## Wiring (Demo: no CAN needed)

```
ESP32 GPIO27 → 330Ω → LED strip DIN
LED strip GND → common GND
ESP32 GND    → common GND
5V supply +  → LED strip 5V (with 1000µF cap across +/−)
USB          → ESP32 power and upload
```

> [!WARNING]
> Never connect the LED strip's 5V to ESP32's 3.3V or VIN pins. Use a separate regulated supply for the LEDs. Always maintain a common ground between ESP32 and the LED supply.

## Passive CAN wiring (SN65HVD230)

For CAN Capture mode only. Passive: no frames are ever transmitted.

```
SN65HVD230 Pin 8 (Rs)  → GND          (normal high-speed mode)
SN65HVD230 Pin 1 (TXD) → 3.3V         (hardware-enforced recessive, never transmit)
SN65HVD230 Pin 4 (RXD) → ESP32 GPIO16  (TWAI receive)
ESP32 GPIO17            → not connected (physically isolated)
Motorcycle CAN_H        → SN65HVD230 CANH
Motorcycle CAN_L        → SN65HVD230 CANL
120Ω termination resistor → remove from module (do not add extra load to vehicle bus)
```

> [!CAUTION]
> CAN hardware qualification is required before connecting to a motorcycle. Software listen-only mode alone does not guarantee electrical safety. The GPIO17 TX pin must remain physically disconnected from the transceiver TXD input.

## Automatic error logging

PulseShift automatically logs errors to internal flash (LittleFS) with no action needed:

- CAN initialization failures and bus errors
- Driver queue overruns and hardware FIFO overruns
- Unexpected reset reasons
- Storage mount failures
- Stale/missing RPM events

Logs survive power-off and ESP32 restarts. They are never erased on reconnection.

## Serial Monitor commands (115200 baud)

Open Serial Monitor in Arduino IDE (Ctrl+Shift+M), set baud to **115200**.

| Command | Description |
|---|---|
| `HELP` | Show available commands |
| `STATUS` | Current system state, storage, CAN controller stats |
| `LOGS` | Display saved error log |
| `DUMP` | Export raw CAN capture as CSV (copy from Serial Monitor) |
| `STAGE <1\|2\|3>` | Set field capture stage (1=KeyOn, 2=Idle, 3=GentleRPM) |
| `CLEAR YES` | Erase stored logs after explicit confirmation |

Demo-mode extra commands:

| Command | Description |
|---|---|
| `rpm <value>` | Set synthetic RPM (0-20000) |
| `demo start` | Resume automated sweep |
| `demo stop` | Pause synthetic frames |
| `profile <classic\|racing\|minimal>` | Change LED style |
| `brightness <0-255>` | Adjust brightness |

## Configuration (`UserConfig.h`)

Edit `PulseShift/UserConfig.h` before uploading:

| Setting | Default | Notes |
|---|---|---|
| `PULSESHIFT_OPERATING_MODE` | `DEMO` | Change to `CAN_CAPTURE` for motorcycle recording |
| `PULSESHIFT_LED_DATA_GPIO` | `27` | LED strip DIN via 330Ω |
| `PULSESHIFT_LED_COUNT` | `6` | Number of LEDs (1-32) |
| `PULSESHIFT_LED_BRIGHTNESS` | `40` | 0-255 (not a hardware current limit) |
| `PULSESHIFT_DEMO_SHIFT_RPM` | `6000` | RPM where shift alert triggers |
| `PULSESHIFT_CAN_BITRATE` | `0` | Set to bitrate (e.g. 500000) for CAN modes |

## What still requires real motorcycle testing

- **The NS125 RPM CAN ID and decoding formula are unknown.** No CAN ID has been identified from the motorcycle's bus. The firmware deliberately shows "unavailable" rather than guessing.
- **CAN bitrate is unverified.** The bus may operate at 125, 250, or 500 kbps. CAN Capture mode will only work after setting the correct bitrate in `UserConfig.h`.
- **Hardware qualification**: SN65HVD230 wiring, transceiver RS pin mode, termination removal, and automotive protection must be verified by a qualified person before connecting to the vehicle.
- **Shift RPM thresholds**: the 6000 RPM shift point is a placeholder. Real calibration depends on the engine and rider preference.

## Safety notes

- GPIO17 (CAN TX) must remain physically isolated from the SN65HVD230 TXD input at all times during motorcycle use.
- PulseShift never transmits CAN frames. The TWAI controller is configured in listen-only mode.
- Do not change wiring while powered. Power LEDs before connecting USB.
- Some 5V WS2812B strips require a 74AHCT125 level shifter for reliable 3.3V signal recognition from the ESP32.

## Directory structure

```
PulseShift-Firmware/
├── PulseShift/           ← Open PulseShift.ino here in Arduino IDE
│   ├── PulseShift.ino    ← Main sketch entry point (setup and loop)
│   ├── UserConfig.h      ← Edit this to change settings
│   └── src/              ← Implementation compiled recursively (no tab clutter)
│       ├── config.h          ← Internal config
│       ├── can_transport.*   ← TWAI driver wrapper (listen-only)
│       ├── can_recorder.*    ← Raw CAN capture to LittleFS
│       ├── shift_light.*     ← WS2812B LED control
│       ├── rpm_decoder.*     ← CAN RPM decoding framework
│       ├── diag_logger.*     ← Automatic error logging
│       ├── storage_manager.* ← LittleFS and NVS management (safe formatting)
│       ├── dashboard.*       ← OLED display (optional)
│       └── ...               ← Supporting headers and sources
├── test/                 ← Host-side C++ and Python verification tests
├── tools/                ← can_investigate.py for log analysis
├── docs/                 ← Hardware and CAN investigation notes
└── README.md
```

## Copyright

Copyright © 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
