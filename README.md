# REVIX LINK

ESP32 shift-light firmware for six WS2812B LEDs, an SN65HVD230 transceiver, and an optional SSD1306 128x64 OLED. The intended motorcycle is the **2025 Indian Bajaj Pulsar NS125 ABS**.

**Current compatibility:** the default is a working software demonstration. The exact motorcycle harness, bitrate and passive RPM mapping are not verified. This is a preview for simulation and isolated bench development; a motorcycle plug-and-play release requires those measurements.

## Run the default simulation

Develop entirely in Ubuntu WSL. Open the repository in VS Code through **Remote WSL**, with PlatformIO and the licensed Wokwi extension installed in that environment.

1. Open the repository folder, containing `platformio.ini`, `wokwi.toml` and `diagram.json`.
2. Press **Ctrl+Shift+B** and wait for **SUCCESS**.
3. Press **F1 -> Wokwi: Start Simulator**.
4. Stop Wokwi, rebuild and restart after code changes.

The default `esp32dev_wokwi` environment enables the OLED and runs `DEMO_SIMULATED`. A 20-second sequence sends complete synthetic CAN frames through the same decoder and RPM state used by reception: valid frames, wrong DLC, out-of-range data, silence, then recovery. LEDs blank and the OLED shows `-- RPM` when valid data becomes stale. The startup sweep is identified as `STARTUP`, separately from RPM indication.

Wokwi does not compile firmware. If it reports a missing configuration, open the repository root and select its `wokwi.toml`. No preparation script or configuration generator is required.

Equivalent WSL commands:

```sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi
bash test/run.sh
```

## Default configuration

Edit **UserConfig.h** for supported user settings; internal source edits are unnecessary.

| Setting | Default |
| --- | --- |
| Operating mode | DEMO; synthetic frames, no CAN transmission |
| LEDs | Six WS2812B, GPIO27, GRB / 800 kHz |
| Brightness | 40/255; a scale, not a hardware current limiter |
| Style / direction | Classic / normal |
| Startup / smoothing | Enabled / enabled |
| Demonstration progression | 1500 start, 5800 shift, 6500 maximum RPM |
| Shift alert / hysteresis | Red flash at 10 Hz / 150 RPM |
| Optional idle effect | Disabled |
| Serial | 115200 baud, diagnostics enabled |
| OLED | Wokwi enabled; packaged Arduino sketch disabled |
| CAN RX / TX | GPIO16 / GPIO17 |
| Bench CAN bitrate | 0, controller disabled |

RPM thresholds are demonstration settings, not a motorcycle calibration or redline. Classic, Racing, Minimal and Custom styles; normal/reversed direction; 1..32 pixels; startup, smoothing and four alert behaviors are supported. Set `REVIX_CUSTOM_COLORS` to RGB triples for a custom palette. Explicit black entries remain dark. Match the configured count to the physical chain.

## Serial demonstration controls

Type commands in the **Wokwi Terminal**, or the serial monitor of a standalone demo board. Commands end with newline. They are compiled only into demonstration firmware with diagnostics enabled.

| Command | Effect |
| --- | --- |
| `help`, `status` | Available commands and current state |
| `demo start` | Resume the automated full-frame replay |
| `demo stop` | Stop all replay frames; RPM becomes stale after 1000 ms |
| `rpm 4200` | Resume frames at synthetic 4200 RPM |
| `rpm 6501` | Exercise decoder rejection above the default range |
| `profile classic`, `profile racing`, `profile minimal` | Change the visual style |
| `brightness 40` | Set brightness in 0..255 |

`rpm` accepts decimal 0..20000; values above the configured demo maximum are rejected by the decoder instead of clamped into a valid reading. Malformed numbers and overlong/binary commands are rejected. Runtime settings are temporary and reset at restart. No command writes an RPM value directly to the display or LEDs.

## Real hardware and bench reception

Use the following **component-side** assignments on an isolated bench. IC pin numbers are not necessarily module header positions.

| Component connection | ESP32 / supply |
| --- | --- |
| SN65HVD230 R/RXD, IC pin 4 | GPIO16 |
| SN65HVD230 D/TXD, IC pin 1 | GPIO17; external recessive pull-up to 3.3 V |
| SN65HVD230 VCC, IC pin 3 / GND, pin 2 | Regulated 3.3 V / logic ground |
| SN65HVD230 RS, IC pin 8 | Fixed high at 3.3 V for passive reception, including reset |
| WS2812B DIN | GPIO27 through an appropriate 3.3-to-5 V data buffer |
| WS2812B power / ground | Suitable regulated LED supply / common logic ground |
| Optional SSD1306 SDA / SCL | GPIO21 / GPIO22, ESP32-safe pull-ups |

Inspect the module schematic before wiring RS: some modules tie it low. Never short a grounded RS pin to 3.3 V. Inspect any onboard 120-ohm termination before connecting to an existing network. CANH/CANL belong only on a verified CAN bus; no motorcycle connector assignment is supplied or implied.

Supporting parts are required: a protected and fused automotive power converter, reverse-polarity/transient protection, a regulated 3.3 V transceiver rail, suitable 5 V LED supply, local bypass/bulk capacitors, data buffer such as a suitable AHCT device, data resistor where required, transceiver TXD pull-up, and a verified harness/enclosure. Use at least 100 nF close to transceiver VCC/GND. A conservative budget for six older 60 mA/pixel devices is 360 mA for LEDs alone; check the exact strip revision and size the supply for the ESP32 and other loads too.

The simulator's TX2-to-RX2 wire is a digital self-test loopback. **Remove that wire for real hardware.** Use the transceiver instead. Never apply vehicle voltage to GPIO or power the LEDs from a GPIO.

For a known isolated CAN bench, set `REVIX_CAN_BITRATE` in `UserConfig.h` to the bench's verified bitrate and build:

```sh
~/.platformio/penv/bin/pio run -e esp32dev_bike
```

This environment forces receive-only mode, reads that bitrate setting, excludes the synthetic ECU, and uses `TWAI_MODE_LISTEN_ONLY`. GPIO17 output is disconnected before controller start; hardware RS-high protection is still required. The RPM mapping remains disabled, so reception logs frames rather than guessing engine RPM. A normal transmitter needs another ACK-capable node on the bench. Error-alert logs are aggregated; driver loss/error counters remain visible. This is a bounded logger, not a lossless high-load CAN analyzer.

After a qualified setup has verified the wiring and bitrate, the optional **Tasks: Run Task -> REVIX: Record bench CAN log** task uses PlatformIO's existing serial monitor to save complete frame/status output under the project's ignored `logs/` directory. It opens an already available WSL serial device; it does not build, upload, discover connector pins, or identify an RPM mapping. If several serial devices exist, select the correct board; if none is exposed to WSL, there is no capture. Check the firmware banner is `BENCH_LISTEN_ONLY` and distinguish it from `DEMO_SIMULATED` before treating any recording as vehicle evidence. Preserve the bitrate, reference RPM measurements and error/drop counters alongside the log. Stop with Ctrl+C. Recorded logs are excluded from Git and source exports.

A physical bench test must verify bus levels and passivity through reset, startup, disconnection and errors. Do not flash the simulator or experimental self-test onto a motorcycle.

## Motorcycle evidence and remaining verification

Bajaj's [NS125 ABS owner manual, revision 00, September 2025](https://cdn.bajajauto.com/-/media/assets/bajajauto/customer-service/owners-manual/owners-manual-pdf/owner-manual-pdf-2026/pulsar-ns125_led-bt-abs.pdf) identifies the relevant model and documents an instrument indication above 9000 RPM. That is not a verified RPM frame encoding or an aftermarket shift recommendation. The older [EU5 training manual](https://bajajmotos.es/wp-content/uploads/2025/05/bajaj-motos-manual-pulsar-ns-125-euro-es.pdf) describes RPM over CAN for that variant; its wiring/protocol cannot be substituted for the 2025 Indian ABS model.

Before vehicle use, obtain the exact connector view/pinout and ECU/cluster variant, establish bitrate and passive RPM frames, and corroborate the mapping against an independent RPM reference over repeated controlled observations. No diagnostic requests, spoofed messages or active discovery are implemented. If RPM requires requests, that is a separate design decision and is not silently enabled here.

Component references: [TI SN65HVD230 datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf), [NeoPixel connections](https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections), [Espressif TWAI](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/twai.html).

## Tests and experimental TWAI target

```sh
bash test/run.sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike
```

The native suite covers decoding, freshness, replay, serial parsing, bounded log queues, warning aggregation, visual configurations, and OLED refresh through test doubles. Host tests do not establish physical device behavior. For an optional repeatable runtime test, temporarily add `rfc2217ServerPort = 4000` under `[wokwi]`, close and reopen the simulator, then run `~/.platformio/penv/bin/python test/wokwi_runtime.py` inside WSL. Keep the simulator visible. This records and asserts real simulator serial output; separately inspect the LEDs/OLED. Remove the port setting and close the session after the test. No serial bridge is required for normal use.

`esp32dev_twai_test` is an experimental transmit/self-receive target at a simulator-only 500 kbit/s. A completed transaction requires matching RX, actual TX-success and zero pending TX. The recorded Wokwi run received one frame but stalled before completion. [Wokwi lists TWAI as partial](https://docs.wokwi.com/guides/esp32). A timeout halts once; no fake completion or automatic fallback is used. Root Wokwi configuration stays on the default replay target.

## Arduino distribution and release preparation

The `.ino` distribution remains supported. Its complete WSL installation, compile and optional USB-upload instructions are included in `REVIX_LINK/README.txt`. The default package needs Adafruit NeoPixel only; enable the documented OLED libraries when opting into the display. All dependencies and compilation stay in WSL.

Release maintenance commands (not required to run Wokwi):

```sh
python3 scripts/package_arduino.py --version v0.2.0 --output-dir .pio/release-work/dist
python3 scripts/verify_arduino_package.py --version v0.2.0 \
  --zip .pio/release-work/dist/REVIX-LINK-Arduino-v0.2.0.zip \
  --extract-to .pio/release-work/extracted-check
```

The ZIP uses an explicit file allowlist, includes COPYRIGHT, and has a SHA-256 checksum. Exact extracted packages must compile before release. Source ZIPs and Arduino packages carry the owner's reserved rights; dependency licenses remain separate. See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md). Public releases require owner approval; the tag workflow publishes only preview versions while vehicle validation is incomplete.

## Troubleshooting

- **No Wokwi firmware:** build first and open the repository root.
- **No serial commands:** use the Wokwi Terminal; check diagnostics are enabled and send a newline.
- **No bench traffic:** bitrate 0 intentionally disables reception. Verify the known bench rate, transceiver power/RS/RXD, wiring and another ACK-capable node.
- **CAN traffic but no RPM:** the vehicle mapping is intentionally disabled until verified.
- **Stale indication:** valid frames stopped or were rejected; inspect frame results and error/loss counters.
- **LEDs flicker or ESP32 resets:** check power capacity, grounding, bypassing and data-level conversion before altering firmware timings.
- **OLED absent:** optional display is disabled in the Arduino package; check its enabled setting, address, wiring and safe pull-ups.

## Copyright

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT). No redistribution license is granted here.
