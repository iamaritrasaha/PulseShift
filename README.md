# REVIX LINK

**ESP32 CAN-bus RPM dashboard prototype designed for the 2025 Bajaj Pulsar NS125 ABS.**

REVIX LINK is an embedded C++ project specifically configured for the **ESP32 DevKit V1**, **six WS2812B RGB LEDs**, **SN65HVD230 CAN transceiver**, and **2025 Bajaj Pulsar NS125 ABS**. It aims to receive engine-speed data from the motorcycle's CAN network and display RPM on an **SSD1306 OLED screen** and RGB shift-light bar. The firmware uses **PlatformIO**, supports **VS Code** development, and includes **Wokwi simulation** modes for testing without a motorcycle.

The hardware targets and pin assignments are tailored to this configuration. Vehicle CAN bitrate, message mapping, and physical integration still require validation.

> **Project status: prototype.** Frame-replay mode exercises CAN-frame decoding and the dashboard. TWAI self-test is experimental in Wokwi, and continuous CAN transmission has **not** been verified there. Real motorcycle CAN bitrate, wiring, and RPM mapping remain unverified. **Do not connect this prototype to a motorcycle without completing bench and electrical validation.**

## Features

- **CAN-frame-based design:** dashboard RPM is derived through the common frame-processing and validation path, not a potentiometer.
- **OLED RPM dashboard:** engine speed, progress bar, and connection/status information.
- **Six RGB shift lights:** green → yellow → red progression with RPM.
- **Two development modes:** synthetic full-frame replay and an experimental ESP32 TWAI self-test.
- **Separate receive-only hardware build:** no simulated ECU, and RPM decoding disabled until the vehicle protocol is verified.
- **Stale-data handling:** distinguishes missing/expired RPM data from genuine zero RPM.
- **Host-side tests:** repeatable C++ decoder/state checks with sanitizers.

## Hardware

| Component | Purpose | ESP32 connections |
| --- | --- | --- |
| ESP32 DevKit V1 | Main controller | Main board |
| SSD1306 128×64 OLED (I²C) | RPM and status | SDA GPIO21, SCL GPIO22 |
| 6 × WS2812B RGB LEDs | Shift-light bar | Data GPIO27 |
| SN65HVD230 CAN transceiver | Physical CAN interface, for later bench tests | RXD GPIO16, TXD GPIO17 |

**Simulation is not physical CAN wiring.** The ESP32 TWAI controller requires a suitable external transceiver to interface with CAN-H/CAN-L. Hardware builds also require verified voltage levels, suitable LED power/data interfacing, and protection against the motorcycle electrical environment.

## Quick start: Wokwi in VS Code

**Prerequisites:** VS Code with WSL support (if developing on Windows/Ubuntu WSL), [PlatformIO IDE](https://platformio.org/install/ide?install=vscode), and [Wokwi for VS Code](https://docs.wokwi.com/vscode/getting-started) with access activated as required.

```bash
git clone https://github.com/iamaritrasaha/REVIX-LINK.git
cd REVIX-LINK
code .
```

If using Windows with WSL, run these commands **inside Ubuntu** and confirm VS Code is attached to **WSL: Ubuntu**. PlatformIO and Wokwi need to be available in that remote environment.

Build the default Wokwi firmware:

```bash
pio run -e esp32dev_wokwi
```

If `pio` is installed but not on the shell path, try `~/.platformio/penv/bin/pio` instead.

Then open the VS Code Command Palette (`Ctrl+Shift+P`) and select **Wokwi: Start Simulator**. The repository's `wokwi.toml` and `diagram.json` must remain next to `platformio.ini`. After code changes, **stop the simulator, rebuild, and restart it**.

## Choose a simulation mode

Edit `include/config.h`:

```cpp
#define REVIX_MODE REVIX_REPLAY
```

| Mode | What it does | What it proves |
| --- | --- | --- |
| `REVIX_REPLAY` **(default)** | Sends complete synthetic CAN frames through the shared decoder | Frame parsing, RPM state, and OLED/LED behavior, **not** physical CAN/TWAI |
| `REVIX_TWAI_TEST` | Uses ESP32 TWAI self-reception in Wokwi | Experimental controller behavior; repeatable completion **not yet verified** |
| `REVIX_BIKE` | Separate physical receive-only build | For controlled bench reception after wiring/bitrate verification; **not** a validated bike RPM reader |

Change the mode, rebuild, and restart Wokwi. No potentiometer is needed. Example CAN IDs and RPM encodings used in demonstrations are **not confirmed Bajaj protocol values**.

### Why the TWAI test is experimental

The observed Wokwi self-test received a frame but did not confirm TX completion; a transmission remained pending. The firmware includes timeout/status diagnostics and halts the failed test rather than flooding the log. Wokwi documents [partial ESP32 TWAI support](https://docs.wokwi.com/guides/esp32), so simulator behavior must not be treated as proof of a working vehicle CAN interface.

## Build and test

```bash
# Default Wokwi firmware
pio run -e esp32dev_wokwi

# Separate receive-only hardware build (not ready for vehicle hookup)
pio run -e esp32dev_bike

# Host-side C++ tests; requires g++
bash test/run.sh
```

The hardware build intentionally defaults to **bitrate 0 (TWAI stopped)** and does not decode actual NS125 RPM data. Configuring a bitrate requires verification first.

## Project structure

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Startup, main loop, and application flow |
| `include/config.h` | Modes, pins, rates, and thresholds |
| `src/can_transport.cpp` | TWAI monitoring and self-test |
| `src/rpm_decoder.cpp` | CAN-frame checks and RPM validity/freshness |
| `src/dashboard.cpp` | SSD1306 OLED and LED outputs |
| `src/simulated_ecu.cpp` | Synthetic CAN frames for development |
| `src/frame_log.cpp` | Bounded CAN diagnostics |
| `test/` | Host-side decoder/state tests |
| `diagram.json`, `wokwi.toml` | Wokwi circuit and firmware loading |

## Roadmap

- [x] PlatformIO-based ESP32 application
- [x] OLED dashboard and six-LED RPM indicator
- [x] CAN-frame replay and shared RPM processing
- [x] Host-side decoding tests
- [ ] Verify continuous TWAI TX/RX behavior with a suitable simulator or bench hardware
- [ ] Validate electrical passivity and protected CAN reception on an isolated bench
- [ ] Confirm the **2025 NS125 ABS** CAN network, connector, bitrate, message ID, and RPM encoding
- [ ] Validate actual motorcycle RPM readings before enabling them in the dashboard
- [ ] Complete installation-grade power, protection, enclosure, and reliability testing

## Safety and current limitations

This repository is **not a validated plug-and-play motorcycle accessory**. The SN65HVD230 needs correct transceiver wiring and operating configuration; never connect CAN-H/CAN-L or a 12 V vehicle supply directly to ESP32 GPIO. Verify grounding, termination, automotive power protection, the transceiver module schematic, and receive-only behavior in isolation before any vehicle work.

Do not flash the Wokwi build for use on a motorcycle. No successful real-motorcycle CAN capture or RPM reading is claimed.

## Contributing

Bug reports, reproducible Wokwi/TWAI findings, test improvements, documentation fixes, and hardware-bench observations are welcome through [GitHub Issues](https://github.com/iamaritrasaha/REVIX-LINK/issues). Please keep vehicle-specific protocol claims backed by measurements or documentation, and **do not post private vehicle identifiers or sensitive captures**.

## Copyright

Copyright © 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
