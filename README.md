# REVIX LINK

ESP32 Arduino C++ firmware for the **2025 Bajaj Pulsar NS125 ABS**, using an **SN65HVD230** CAN transceiver and **six WS2812B LEDs**. The existing SSD1306 OLED is retained. **Start with `src/main.cpp`.** Settings are in `include/config.h`.

## Run it

1. Open Ubuntu (WSL) and run:
   ```sh
   cd /home/hrik/projects/REVIX_LINK_ESP32
   code .
   ```
2. In VS Code, confirm **WSL: Ubuntu** at the bottom.
3. Press **Ctrl+Shift+B** (**Build REVIX LINK**) and wait for **SUCCESS**.
4. Press **F1**, select **Wokwi: Start Simulator**.

The circuit contains the ESP32, OLED and six LEDs. Read CAN diagnostics in the Wokwi terminal. After editing code, stop Wokwi, build again, and restart it.

**No preparation scripts or Python commands are needed.** PlatformIO handles its own build tools and libraries in WSL.

Keep `wokwi.toml` and `diagram.json` here beside `platformio.ini`. If Wokwi says the config is missing, open this project folder (not `src/`) and use **Wokwi: Select Config File** to select this folder's `wokwi.toml`.

## Choose the test

In `include/config.h`, this is the only simulation switch:

```cpp
#define REVIX_MODE REVIX_REPLAY
```

- `REVIX_REPLAY` (currently selected): feeds complete synthetic CAN frames through the decoder to test the OLED/LEDs. The banner says `FRAME_REPLAY_TEST`. This does **not** test TWAI or a physical CAN bus.
- `REVIX_TWAI_TEST`: exercises TWAI self-reception with the dashboard. A transaction requires matching RX, TX_SUCCESS and zero pending TX. The user's latest Wokwi run received one frame but stalled before TX completion; this mode halts with a diagnostic.

Change the value, build, and restart Wokwi. Neither choice requires changing paths or the circuit. Replay ignores the simulator's loopback wire.

## Where the code is

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Arduino `setup()` / `loop()` and application flow |
| `include/config.h` | Simulation mode, pins and timing |
| `src/can_transport.cpp` | TWAI reception and self-test handling |
| `src/rpm_decoder.cpp` | CAN validation and RPM freshness |
| `src/dashboard.cpp` | OLED and six LEDs |
| `src/simulated_ecu.cpp` | Complete demonstration CAN frames |
| `src/frame_log.cpp` | Bounded serial frame logging |

All displayed RPM passes through frame validation. No potentiometer is used.

## Optional checks

In the project's WSL terminal:

```sh
~/.platformio/penv/bin/pio run
bash test/run.sh
```

The first command builds the same Wokwi firmware as Ctrl+Shift+B. The second runs C++ tests and requires Ubuntu's `g++`.

VS Code needs WSL support, with PlatformIO IDE and Wokwi installed in **WSL: Ubuntu**, plus a Wokwi license. These tools are already set up in this workspace. No Windows ESP/Arduino toolchain is required.

## Real hardware

`esp32dev_bike` is a separate receive-only build. It overrides the simulation switch, excludes the synthetic ECU, disables RPM decoding, and defaults to bitrate 0 (CAN stopped). The 2025 Pulsar NS125 ABS bitrate and RPM mapping are unverified. Do not flash the Wokwi firmware to the motorcycle.

Read [hardware requirements](docs/HARDWARE.md), [CAN findings](docs/CAN-INVESTIGATION.md), [testing](docs/TESTING.md), and [actual verification results](docs/VERIFICATION.md) before further hardware work.
