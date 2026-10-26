# REVIX LINK

The standard setup is an **ESP32 DevKit V1 and six WS2812B LEDs in standalone simulated RPM mode**. It works without CAN and needs no settings changed. An SN65HVD230 is used only for the separate CAN receiver architecture; an SSD1306 OLED is optional. The intended motorcycle is the **2025 Indian Bajaj Pulsar NS125 ABS**, with integration still unverified.

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
cd /home/hrik/projects/REVIX_LINK_ESP32
~/.platformio/penv/bin/pio run -e esp32dev_wokwi
bash test/run.sh
```

## Default configuration

Keep **UserConfig.h** unchanged for standard hardware. Its comments explain every advanced option.

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

## Standard standalone LED wiring

No CAN transceiver, motorcycle or OLED is needed for this demonstration. Keep GPIO16 and GPIO17 unconnected and reserved for CAN. Use an original ESP32 DevKit V1 and a six-pixel **5V WS2812B GRB / 800kHz** strip. No setting needs editing.

Parts in addition to the ESP32 and strip:

- One **74AHCT125**, preferably the breadboard-friendly SN74AHCT125N DIP-14. Do not substitute a 74HC125 or a bidirectional I2C level-shifter board.
- One **330-ohm** series data resistor, one **10k-ohm** input pull-down resistor, one **1000uF 10V** electrolytic capacitor, and one **100nF (0.1uF)** ceramic bypass capacitor.
- One regulated **5V supply rated at least 1A** for the strip and shifter, a USB data cable for the ESP32, secure wiring/connectors and a breadboard or soldered assembly. Use a multimeter to check supply voltage and polarity before connecting the electronics.

The table uses DIP-14 IC pin numbers, not breakout-board header positions. Viewed from above with the notch upward, pin 1 is top left; pins 1..7 run down the left side and 8..14 run up the right side. Check the actual package marking.

| From | To |
| --- | --- |
| Regulated external +5V | Strip +5V and 74AHCT125 VCC pin 14 |
| External supply negative | Strip GND, ESP32 GND and shifter GND pin 7, all joined |
| ESP32 GPIO27 | Shifter 1A pin 2; also connect a 10k-ohm resistor from pin 2 to GND |
| Shifter 1OE pin 1 | GND, enabling the first buffer |
| Shifter 1Y pin 3 | 330-ohm resistor, then first pixel DIN; put the resistor close to DIN |
| 1000uF 10V capacitor | Across strip +5V and GND near the input; positive lead to +5V, negative striped side to GND |
| 100nF ceramic capacitor | Between shifter pins 14 and 7, with short leads close to the IC |
| Unused shifter OE pins 4, 10, 13 | +5V to disable the unused buffers |
| Unused shifter A pins 5, 9, 12 | GND; leave unused output pins 6, 8, 11 unconnected |
| Last pixel DOUT | Unconnected |

Follow the strip's printed arrow: data travels from **DIN -> pixel 1 -> pixel 6 -> DOUT**. Never feed DOUT. Verify labels instead of trusting wire colors. Reversed software direction changes the visual fill order only. Keep signal wiring short and route a ground wire alongside it. The pull-down holds the shifter input low while the ESP32 pin is not driving it. The 5V shifter output goes only to LED DIN, never to an ESP32 GPIO.

### Power and connection order

1. Disconnect USB and external power before assembling or changing wiring. Connect common ground first and keep it connected throughout operation. Check capacitor polarity, connections and absence of shorts before powering up.
2. For this standard setup, power the ESP32 through USB only. External 5V powers only the strip and shifter. **Do not join external +5V to the ESP32 5V/VIN/3V3 pins while USB is connected.** The two supplies share ground, not their positive rails.
3. Switch on the regulated strip/shifter 5V supply first, then connect ESP32 USB. This avoids sending data into an unpowered strip. Do not hot-plug the data wire or lose common ground while powered.
4. Disconnect ESP32 USB first, then switch off strip/shifter power. Let the bulk capacitor discharge before rewiring; do not short its leads. Remove ground last when dismantling the unpowered circuit.

At a conservative 60mA per pixel, six pixels need up to 360mA before other loads. The 1A minimum here is for the separate LED/shifter supply; USB powers the ESP32. Brightness 40/255 is not a current limiter. More pixels or another power arrangement require a new current budget and board power-path check. Never power the strip from a GPIO or the ESP32 3V3 regulator. This bench 5V supply arrangement is not an automotive power system.

Wiring references: [TI 74AHCT125 pinout and logic levels](https://www.ti.com/lit/ds/symlink/sn74ahct125.pdf), [Adafruit NeoPixel wiring and power sequence](https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices). This specifies the intended assembly; physical operation still needs checking on your actual parts.

## Settings for nonstandard hardware and personal preferences

Leave every setting unchanged for the standard LED demonstration. `UserConfig.h` labels each editable setting with **DEFAULT**, **CHANGE ONLY IF**, **OPTIONAL**, or **DO NOT CHANGE WITHOUT VERIFICATION**. Symbolic mode/style constants and the validation section are not user settings.

| Reason to customize | Setting and limits |
| --- | --- |
| Different LED wiring or count | `REVIX_LED_DATA_GPIO`, `REVIX_LED_COUNT` (1..32); check wiring and current budget; reserve GPIO16/17 |
| Personal appearance | `REVIX_LED_BRIGHTNESS` (0..255), `REVIX_SHIFT_LIGHT_STYLE` (Classic/Racing/Minimal/Custom), `REVIX_CUSTOM_COLORS` (RGB triples, channels 0..255), `REVIX_LED_DIRECTION` (normal/reversed) |
| Animation or alert preference | `REVIX_STARTUP_ANIMATION_ENABLED`, `REVIX_SMOOTH_TRANSITIONS_ENABLED` (0/1), `REVIX_SHIFT_ALERT_MODE` (off/solid red/flashing red/flashing white), `REVIX_ANIMATION_INTERVAL_MS` (5..250ms) |
| Synthetic scale only | `REVIX_DEMO_START_RPM`, `REVIX_DEMO_SHIFT_RPM`, `REVIX_DEMO_MAX_RPM` (maximum 1..20000; start < shift <= maximum), `REVIX_ALERT_HYSTERESIS_RPM` (0 to less than shift minus start) |
| Optional idle effect | `REVIX_IDLE_EFFECT_ENABLED` (0/1), `REVIX_IDLE_MAX_RPM` (0..shift RPM) |
| Add an OLED | `REVIX_OLED_ENABLED` (0/1), `REVIX_OLED_SDA_GPIO`, `REVIX_OLED_SCL_GPIO`; use a 128x64 SSD1306 at 0x3C, safe 3.3V pull-ups and the display libraries |
| Serial preference | `REVIX_SERIAL_DIAGNOSTICS` (0/1), `REVIX_SERIAL_BAUD` (1..2000000); match the serial monitor rate; diagnostics off also removes runtime demo commands |
| Verified isolated CAN bench only | `REVIX_OPERATING_MODE`, `REVIX_CAN_RX_GPIO`, `REVIX_CAN_TX_GPIO`, `REVIX_CAN_BITRATE`; read the bench requirements before changing them |

GRB / 800kHz is fixed in the existing LED driver, not a `UserConfig.h` option. A different RGB order, RGBW strip or protocol requires a reviewed driver change. Do not change the RGB order of palette triples to compensate. CAN bitrate does not select a motorcycle protocol; no verified vehicle mapping is supplied.

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
| WS2812B data / power | Keep the standard 74AHCT125 wiring and external 5V supply described above |
| Optional SSD1306 SDA / SCL | GPIO21 / GPIO22, ESP32-safe pull-ups |

**A six-pin SN65HVD230 breakout is not automatically safe for motorcycle connection.** Its header may not expose RS. Inspect the actual module schematic/continuity and measure electrical passivity through reset and power transitions before bus connection. Some modules tie RS low. Never short a grounded RS pin to 3.3 V. Inspect any onboard 120-ohm termination before connecting to an existing network. CANH/CANL belong only on a verified CAN bus; no motorcycle connector assignment is supplied or implied.

For any future vehicle installation, additional parts are required: a fused automotive converter with reverse-polarity and transient protection, a regulated 3.3V transceiver rail, transceiver TXD bias, verified connectors/harness and an enclosure. Use at least 100nF close to transceiver VCC/GND. The standard LED bench assembly and a generic six-pin breakout do not provide these protections.

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

## Install the standalone Arduino demonstration in WSL

Use the Arduino source ZIP for the standard LED-only board. Extract it under your WSL home, not `/mnt/c` or `/mnt/d`. Follow [arduino/README.txt](arduino/README.txt) (also included inside the ZIP) for Arduino CLI installation, pinned core/libraries, compilation and USB upload. No settings need editing. The existing WSL installation already has these tools.

After extracting, open a WSL terminal in the `REVIX_LINK` folder and run:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --output-dir build .
```

The guide shows how to select an available WSL USB device and upload the resulting build. USB access in WSL is a prerequisite; if no board appears, arrange that access before uploading. Use USB with the motorcycle disconnected and follow the LED power order above. First boot shows a startup sweep followed by repeating simulated progression, shift flashing, a deliberate stale/off interval, and recovery. A dark interval in this test cycle is expected.

For Arduino IDE users, run the Linux IDE inside WSL2/WSLg, open `REVIX_LINK.ino`, install ESP32 core 2.0.17 and Adafruit NeoPixel 1.12.3, and select **ESP32 Dev Module**. Do not install the toolchain on Windows. The Arduino default disables OLED; the existing PlatformIO `esp32dev_wokwi` target deliberately enables its simulated OLED. Neither `esp32dev_bike` nor `esp32dev_twai_test` is the beginner LED demonstration.

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
