# REVIX LINK

REVIX LINK is an ESP32-based motorcycle RPM shift-light system for an ESP32 DevKit V1, six WS2812B LEDs, an SN65HVD230 CAN transceiver, and the 2025 Bajaj Pulsar NS125 ABS. The motorcycle CAN bitrate, wiring, message mapping, and RPM decoding have not been verified.

The Arduino IDE package defaults to a standalone demonstration. It generates synthetic CAN frames in software and processes them through the existing decoder and visual shift-light engine. Its RPM values are simulated and do not describe the motorcycle. The demonstration does not transmit CAN frames and requires no CAN connection.

> **Latest Preview Release**: Download the verified Arduino IDE package directly from [REVIX LINK v0.1.0 Preview](https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.1.0) (`REVIX-LINK-Arduino-v0.1.0.zip`).

## Visual shift-light system

REVIX LINK prioritizes an LED-first instrument architecture:
- Six WS2812B RGB LEDs on GPIO 27 by default, supporting configurable LED counts.
- Progressive RPM fill with configurable start and shift thresholds.
- Optional smooth partial brightness interpolation using fixed-point math.
- Selectable color profiles:
  - Classic: progressive green, yellow, orange, and red.
  - Racing: vivid racing green, bright amber, crimson, and red.
  - Minimal: clean ice-white progressive fill with red shift point.
  - Custom: user-configurable color array.
- Non-blocking startup sequence with forward sweep, reverse sweep, and confirmation pulse.
- Configurable shift alert with hysteresis to prevent rapid flickering around the threshold. Supported alert modes include Off, Solid Red, Flash Red at 10 Hz, and Flash White at 10 Hz.
- Fail-safe state handling: LEDs blank immediately if communication is disconnected, stale, invalid, or faulted. No stale engine speed is displayed.
- Optional engine idle breathing effect on the first LED during live idle RPM.

## Arduino IDE: for regular users

1. Download `REVIX-LINK-Arduino-v0.1.0.zip` directly from the [REVIX LINK v0.1.0 Release](https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.1.0), or browse all releases on the [GitHub Releases page](https://github.com/iamaritrasaha/REVIX-LINK/releases).
2. Extract the ZIP and keep the `REVIX_LINK` folder intact.
3. Open `REVIX_LINK/REVIX_LINK.ino` in Arduino IDE 2.x.
4. In File > Preferences, add the Espressif Boards Manager index URL:

   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

5. In Boards Manager, install **ESP32 by Espressif Systems**, version **2.0.17**.
6. In Library Manager, install **Adafruit NeoPixel 1.12.3**. If you enable the optional OLED, also install **Adafruit SSD1306 2.5.13**, **Adafruit GFX Library 1.11.11**, and **Adafruit BusIO 1.17.0**.
7. Edit `UserConfig.h` to choose your settings. The defaults are:
   - LED data pin: GPIO 27, count: 6 LEDs, brightness: 40 (0..255).
   - Style: `REVIX_STYLE_CLASSIC` (options: `REVIX_STYLE_RACING`, `REVIX_STYLE_MINIMAL`, `REVIX_STYLE_CUSTOM`).
   - Shift alert: `REVIX_ALERT_FLASH_RED` (options: `REVIX_ALERT_OFF`, `REVIX_ALERT_SOLID_RED`, `REVIX_ALERT_FLASH_WHITE`).
   - Orientation: `REVIX_DIRECTION_NORMAL` (or `REVIX_DIRECTION_REVERSED`).
   - Start RPM: 1500, Shift RPM: 5800, Alert hysteresis: 150 RPM.
   - Smooth transitions: enabled; Startup animation: enabled.
   - OLED display: disabled by default.
8. Select **ESP32 Dev Module** under Tools > Board and the connected board port under Tools > Port.
9. Click **Verify**, then **Upload**. Open Serial Monitor at **115200 baud**.

The downloadable Arduino package disables the OLED by default, so its LED-only build requires only Adafruit NeoPixel. To enable the optional OLED diagnostics in an Arduino IDE build, set `REVIX_OLED_ENABLED` to `1` in `UserConfig.h` and install the OLED libraries listed above.

### Demonstration and bench modes

The default `REVIX_USER_MODE_DEMO` mode exercises synthetic frames, the decoder, and the shift lights. Serial output identifies this mode as simulated. It does not validate the motorcycle protocol or physical CAN hardware.

In demonstration mode, you can interact directly via the Serial Monitor (115200 baud, newline line ending):
- `help`: list available commands.
- `status`: inspect current RPM, alert state, style profile, and brightness.
- `demo start`: resume automated synthetic acceleration and deceleration cycle.
- `demo stop`: pause automated synthetic cycle at current RPM.
- `rpm <val>`: manually inject synthetic RPM (e.g. `rpm 4200` or `rpm 6000`).
- `profile <classic|racing|minimal>`: dynamically switch LED color style.
- `brightness <val>`: adjust master brightness (0..255).

`REVIX_USER_MODE_BENCH_LISTEN_ONLY` is an optional receive-only mode for an isolated bench setup. Its CAN bitrate defaults to `0`, which keeps TWAI stopped. The RPM mapping remains disabled. Choose a supported bitrate only after verifying it on an isolated bench. In this mode, the firmware uses `TWAI_MODE_LISTEN_ONLY` and disconnects the ESP32 CAN TX GPIO from output. The SN65HVD230 must also be held in its hardware silent state by keeping RS high, and TXD must remain recessive with an external pull-up. Software checks do not prove electrical passivity. Do not connect this unverified prototype directly to the motorcycle.

## PlatformIO: for developers

PlatformIO remains supported for development, host-side testing, and Wokwi simulation. Install PlatformIO Core and a C++17 toolchain, then run these commands from the repository root:

```bash
pio run -e esp32dev_wokwi
pio run -e esp32dev_twai_test
pio run -e esp32dev_bike
bash test/run.sh
```

The Wokwi target defaults to synthetic frame replay with optional OLED diagnostics enabled. The separate `esp32dev_twai_test` target is an experimental controller self-test that transmits test frames in Wokwi. Its 500 kbit/s setting is for the simulator only and is not a verified motorcycle bitrate. Do not use it on a motorcycle bus. The `esp32dev_bike` target is receive-only, omits simulated ECU code, and keeps TWAI stopped until a supported bitrate is explicitly configured. It does not decode actual NS125 RPM data.

`diagram.json` and `wokwi.toml` remain beside `platformio.ini` for Wokwi. Restart the simulator after rebuilding firmware. Simulation does not prove physical CAN behavior.

## Arduino package and releases

The tagged-release workflow builds and verifies the extracted Arduino ZIP before attaching it to a GitHub Release. It also publishes a SHA-256 checksum. No release is published until a semantic-version tag such as `v0.1.0` is pushed.

To create and inspect a package locally:

```bash
python3 scripts/package_arduino.py --version v0.1.0 --output-dir /tmp/revix-release
python3 scripts/verify_arduino_package.py \
  --zip /tmp/revix-release/REVIX-LINK-Arduino-v0.1.0.zip \
  --version v0.1.0 \
  --extract-to /tmp/revix-extracted
```

The package includes the REVIX LINK source and preserves its copyright notices. Third-party libraries remain subject to their own licenses. The repository has no explicit `LICENSE` file, so this project is not described as open-source. See [COPYRIGHT](COPYRIGHT) and [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).

## Hardware and current limitations

| Component | Purpose | ESP32 DevKit V1 connections |
| --- | --- | --- |
| ESP32 DevKit V1 | Main controller | Main board |
| Six WS2812B RGB LEDs | RPM shift-light bar | Data GPIO 27 |
| SN65HVD230 CAN transceiver | Isolated bench CAN reception only | RXD GPIO 16, TXD GPIO 17 |
| SSD1306 128x64 OLED | Optional development diagnostics | SDA GPIO 21, SCL GPIO 22 |

Use a suitable transceiver and verify electrical levels, grounding, power, and protection on an isolated bench. Never connect CAN-H, CAN-L, or vehicle power directly to ESP32 GPIO. No successful real-motorcycle CAN capture or RPM reading is claimed.

## Project structure

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Startup, application lifecycle, and demo command parsing |
| `include/config.h` and `UserConfig.h` | Shared firmware settings and user options |
| `include/shift_light.h` and `src/shift_light.cpp` | Shift-light engine, animations, and color profiles |
| `src/can_transport.cpp` | TWAI monitoring and receive-only capture |
| `src/rpm_decoder.cpp` | CAN frame checks and RPM freshness |
| `src/dashboard.cpp` | Optional SSD1306 display and WS2812B output |
| `src/simulated_ecu.cpp` | Synthetic frames for development and demo mode |
| `src/frame_log.cpp` | Bounded serial frame diagnostics |
| `test/` | Host-side decoder, state, LED, animation, and transport tests |
| `scripts/package_arduino.py` | Arduino sketch ZIP packaging |

## Copyright

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
