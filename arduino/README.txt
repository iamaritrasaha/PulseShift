REVIX LINK @VERSION@ - Arduino source preview
============================================
Copyright (c) 2026 Aritra Saha. All rights reserved.

DEFAULT: ESP32 DevKit V1; six WS2812B pixels on GPIO27, GRB/800kHz,
brightness 40/255, Classic/normal, startup and smoothing enabled, red shift
flash, 150 RPM hysteresis. Synthetic start/shift/max = 1500/5800/6500 RPM.
OLED is OFF; serial diagnostics are ON at 115200 baud. No CAN connection is
needed. Leave every setting unchanged for the standard LED demonstration.
GPIO16 and GPIO17 stay unconnected and reserved for the CAN receiver.
These values are demonstrations, not a motorcycle calibration.

Source entry point: src/main.cpp. User settings: UserConfig.h.
Keep this folder intact. REVIX_LINK.ino supplies the Arduino sketch identity;
setup()/loop() are defined once in src/main.cpp.

BUILD INSIDE UBUNTU WSL
----------------------
Extract the ZIP under your WSL home directory, not /mnt/c or /mnt/d.
Open a WSL terminal in the extracted REVIX_LINK folder. Use Arduino CLI
1.5.1 with Espressif ESP32 core 2.0.17. Skip installation if already present.

To install the CLI in WSL (requires curl):
mkdir -p "$HOME/.local/bin"
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh -o /tmp/revix-arduino-cli-install.sh
BINDIR="$HOME/.local/bin" sh /tmp/revix-arduino-cli-install.sh 1.5.1
export PATH="$HOME/.local/bin:$PATH"
arduino-cli version

Use that PATH line in each new shell if ~/.local/bin is not already on PATH.
The installer and all tools remain inside WSL. Install the pinned dependencies:

arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib update-index
arduino-cli lib install --no-deps "Adafruit NeoPixel@1.12.3"
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --output-dir build .

Only NeoPixel is required with OLED disabled. For OLED, set
REVIX_OLED_ENABLED to 1 in UserConfig.h and install:
arduino-cli lib install --no-deps "Adafruit SSD1306@2.5.13"
arduino-cli lib install --no-deps "Adafruit GFX Library@1.11.11"
arduino-cli lib install --no-deps "Adafruit BusIO@1.17.0"

Arduino IDE 2.x can also run inside WSL2/WSLg. Open REVIX_LINK.ino, install
those same pinned dependencies in Boards/Library Manager and select
Tools > Board > ESP32 Arduino > ESP32 Dev Module. Do not install board cores
or libraries on Windows for this project.

USB UPLOAD IS A SEPARATE OWNER ACTION
-------------------------------------
Keep the motorcycle disconnected and follow the LED power order below.
First expose the USB board to WSL using your reviewed USB setup, then use
`arduino-cli board list` to discover its actual Linux port. For a standalone
USB-powered demo board only, upload the compiled sketch using:
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/YOUR_BOARD_PORT --input-dir build .

Replace /dev/YOUR_BOARD_PORT with the actual device reported by board list.
If no board appears, USB has not been exposed to WSL or the cable/driver needs
attention. Do not guess a port. Upload does not compile; run compile first.
Open a monitor at 115200 baud:
arduino-cli monitor --port /dev/YOUR_BOARD_PORT --config baudrate=115200

Do not use a guessed port or run upload on a connected motorcycle. This
package is not a validated plug-and-play motorcycle accessory.

USER SETTINGS FOR NONSTANDARD HARDWARE
--------------------------------------

Leave every setting unchanged for the standard LED demonstration. UserConfig.h labels each editable setting with DEFAULT, CHANGE ONLY IF, OPTIONAL, or DO NOT CHANGE WITHOUT VERIFICATION. Symbolic mode/style constants and the validation section are not user settings.

| Reason to customize | Setting and limits |
| --- | --- |
| Different LED wiring or count | REVIX_LED_DATA_GPIO, REVIX_LED_COUNT (1..32); check wiring and current budget; reserve GPIO16/17 |
| Personal appearance | REVIX_LED_BRIGHTNESS (0..255), REVIX_SHIFT_LIGHT_STYLE (Classic/Racing/Minimal/Custom), REVIX_CUSTOM_COLORS (RGB triples, channels 0..255), REVIX_LED_DIRECTION (normal/reversed) |
| Animation or alert preference | REVIX_STARTUP_ANIMATION_ENABLED, REVIX_SMOOTH_TRANSITIONS_ENABLED (0/1), REVIX_SHIFT_ALERT_MODE (off/solid red/flashing red/flashing white), REVIX_ANIMATION_INTERVAL_MS (5..250ms) |
| Synthetic scale only | REVIX_DEMO_START_RPM, REVIX_DEMO_SHIFT_RPM, REVIX_DEMO_MAX_RPM (maximum 1..20000; start < shift <= maximum), REVIX_ALERT_HYSTERESIS_RPM (0 to less than shift minus start) |
| Optional idle effect | REVIX_IDLE_EFFECT_ENABLED (0/1), REVIX_IDLE_MAX_RPM (0..shift RPM) |
| Add an OLED | REVIX_OLED_ENABLED (0/1), REVIX_OLED_SDA_GPIO, REVIX_OLED_SCL_GPIO; use a 128x64 SSD1306 at 0x3C, safe 3.3V pull-ups and the display libraries |
| Serial preference | REVIX_SERIAL_DIAGNOSTICS (0/1), REVIX_SERIAL_BAUD (1..2000000); match the serial monitor rate; diagnostics off also removes runtime demo commands |
| Verified isolated CAN bench only | REVIX_OPERATING_MODE, REVIX_CAN_RX_GPIO, REVIX_CAN_TX_GPIO, REVIX_CAN_BITRATE; read the bench requirements before changing them |

GRB / 800kHz is fixed in the existing LED driver, not a UserConfig.h option. A different RGB order, RGBW strip or protocol requires a reviewed driver change. Do not change the RGB order of palette triples to compensate. CAN bitrate does not select a motorcycle protocol; no verified vehicle mapping is supplied.


SERIAL CONTROLS
---------------
UserConfig.h contains mode, pins, 1..32 pixels, brightness, style, direction,
startup, smoothing, custom RGB palette, alert mode, thresholds, and optional
OLED/serial settings. Runtime changes are not persisted across restart.

Serial Monitor commands, terminated with newline:
help / status          inspect commands/current state
demo start             resume the automated full-frame cycle
demo stop              stop ALL replay frames; RPM becomes stale after 1000ms
rpm 4200               resume frames at synthetic 4200 RPM
rpm 6501               default decoder rejects this above-range value
profile classic        also supports racing and minimal
brightness 40          valid decimal range 0..255

Malformed numeric values and overlong/binary commands are rejected. Every RPM
command generates a complete synthetic frame through the production decoder;
there is no direct RPM-to-LED assignment. Startup is a separate animation.

OPTIONAL OLED
-------------
SSD1306 128x64, address 0x3C, SDA21/SCL22 with ESP32-safe pull-ups. The display
shows REVIX LINK, simulated mode, validated RPM or --, CAN status and a bar.
The 20-second automated cycle includes malformed/out-of-range frames, silence
and recovery, with or without an OLED. After the startup sweep, the LEDs show
progression and shift flashing, then go dark when valid RPM becomes stale, and
resume when valid frames return. The deliberate dark interval is expected.

ISOLATED BENCH CAPTURE
----------------------
Select REVIX_USER_MODE_BENCH_LISTEN_ONLY and configure REVIX_CAN_BITRATE only
for a known bench rate. Default bitrate 0 leaves TWAI stopped. RPM mapping is
disabled; this mode logs frames and cannot report genuine NS125 engine RPM.

SN65HVD230: regulated 3.3V VCC, GND to logic ground, R/RXD to GPIO16,
D/TXD to GPIO17 with external recessive pull-up, RS held high including reset.
A six-pin breakout is not automatically safe for motorcycle connection.
Its RS pin may be hidden or tied low; module header positions differ from IC
pins. Check the actual RS circuit, termination and electrical passivity first.
Do not short a grounded RS pin to 3.3V.
The controller is listen-only and TX17 output is disconnected before start.
Hardware passivity still requires bench measurement. Another bench node must
acknowledge a normal transmitter; this passive device does not ACK frames.

STANDARD STANDALONE LED WIRING
------------------------------

No CAN transceiver, motorcycle or OLED is needed for this demonstration. Keep GPIO16 and GPIO17 unconnected and reserved for CAN. Use an original ESP32 DevKit V1 and a six-pixel 5V WS2812B GRB / 800kHz strip. No setting needs editing.

Parts in addition to the ESP32 and strip:

- One 74AHCT125, preferably the breadboard-friendly SN74AHCT125N DIP-14. Do not substitute a 74HC125 or a bidirectional I2C level-shifter board.
- One 330-ohm series data resistor, one 10k-ohm input pull-down resistor, one 1000uF 10V electrolytic capacitor, and one 100nF (0.1uF) ceramic bypass capacitor.
- One regulated 5V supply rated at least 1A for the strip and shifter, a USB data cable for the ESP32, secure wiring/connectors and a breadboard or soldered assembly. Use a multimeter to check supply voltage and polarity before connecting the electronics.

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

Follow the strip's printed arrow: data travels from DIN -> pixel 1 -> pixel 6 -> DOUT. Never feed DOUT. Verify labels instead of trusting wire colors. Reversed software direction changes the visual fill order only. Keep signal wiring short and route a ground wire alongside it. The pull-down holds the shifter input low while the ESP32 pin is not driving it. The 5V shifter output goes only to LED DIN, never to an ESP32 GPIO.

POWER AND CONNECTION ORDER
--------------------------

1. Disconnect USB and external power before assembling or changing wiring. Connect common ground first and keep it connected throughout operation. Check capacitor polarity, connections and absence of shorts before powering up.
2. For this standard setup, power the ESP32 through USB only. External 5V powers only the strip and shifter. Do not join external +5V to the ESP32 5V/VIN/3V3 pins while USB is connected. The two supplies share ground, not their positive rails.
3. Switch on the regulated strip/shifter 5V supply first, then connect ESP32 USB. This avoids sending data into an unpowered strip. Do not hot-plug the data wire or lose common ground while powered.
4. Disconnect ESP32 USB first, then switch off strip/shifter power. Let the bulk capacitor discharge before rewiring; do not short its leads. Remove ground last when dismantling the unpowered circuit.

At a conservative 60mA per pixel, six pixels need up to 360mA before other loads. The 1A minimum here is for the separate LED/shifter supply; USB powers the ESP32. Brightness 40/255 is not a current limiter. More pixels or another power arrangement require a new current budget and board power-path check. Never power the strip from a GPIO or the ESP32 3V3 regulator. This bench 5V supply arrangement is not an automotive power system.

Wiring references: [TI 74AHCT125 pinout and logic levels](https://www.ti.com/lit/ds/symlink/sn74ahct125.pdf), [Adafruit NeoPixel wiring and power sequence](https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices). This specifies the intended assembly; physical operation still needs checking on your actual parts.


For vehicle integration, a fused automotive power converter with reverse-
polarity/transient protection, verified grounding/termination, connectors and
enclosure is required. Never connect vehicle voltage to GPIO. Never use a
GPIO-to-GPIO simulator loopback instead of a real transceiver.

VEHICLE COMPATIBILITY
---------------------
Intended target: 2025 Indian Bajaj Pulsar NS125 ABS. The exact harness,
connector pinout, bitrate, passive CAN ID and RPM encoding are unverified.
No ECU requests, commands or spoofed messages are implemented. The older
EU5 manual cannot establish the Indian 2025 protocol. Actual compatibility
requires a variant-specific wiring diagram and corroborated passive capture.

TROUBLESHOOTING
---------------
No LEDs: check power/ground, DIN direction, count and logic-level conversion.
Resets/flicker: check power and wiring before changing software timing.
No capture: bitrate 0 is deliberately disabled; verify bench rate and RX wiring.
Traffic but no RPM: motorcycle decoding is disabled until its mapping is verified.
Stale RPM: valid frames stopped or were rejected; resume valid frames to recover.
No OLED: default is OFF; enable it, install libraries, verify wiring/address.

REPOSITORY DEVELOPMENT AND WOKWI (OPTIONAL)
-------------------------------------------
These steps use the repository, not the downloaded Arduino sketch folder.
Open /home/hrik/projects/REVIX_LINK_ESP32 in VS Code through Remote WSL.
Use PlatformIO and the licensed Wokwi extension in that WSL environment.
Press Ctrl+Shift+B, wait for SUCCESS, then F1 > Wokwi: Start Simulator.
Stop the simulator, rebuild and restart after code changes. Root wokwi.toml
points to the esp32dev_wokwi firmware; Wokwi does not compile it for you.
That environment enables its simulated OLED. The Arduino default keeps it off.

From the repository root in WSL:
bash test/run.sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike

The replay demo tests complete frames through the shared decoder and state.
It does not test physical CAN. esp32dev_twai_test transmits experimental
self-test frames at a simulator-only 500kbit/s. Wokwi's partial TWAI support
has reproduced one RX frame with no TX completion. A timeout halts once;
self-reception alone is not proof of completed transmission.

esp32dev_bike forces listen-only mode, honors the verified bench bitrate in
UserConfig.h, and excludes the synthetic ECU. It keeps motorcycle decoding
disabled. The optional VS Code task "REVIX: Record bench CAN log" captures
serial logs from already installed bench firmware; it does not flash a board.
Do not upload either CAN target as the standard standalone demonstration.

README illustrations are editable SVGs generated by the standard-library-only
assets/readme/generate.py in the repository. Regenerate with:
python3 assets/readme/generate.py
No image-generation step is needed to run or build the firmware. Those images
are displayed by the repository README; this source ZIP keeps the complete
text wiring guide above and does not depend on images outside the package.

References:
https://docs.arduino.cc/arduino-cli/installation/
https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf
https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections
https://www.bajajauto.com/customer-service/owners-manual

Third-party libraries retain their respective notices/licenses. REVIX LINK
is governed by the included COPYRIGHT notice; no new license is granted.
