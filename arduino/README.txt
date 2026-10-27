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
Select REVIX_USER_MODE_CAN_CAPTURE and configure REVIX_CAN_BITRATE only
for a known bench rate. Default bitrate 0 leaves TWAI stopped. RPM mapping is
disabled; this mode logs frames and cannot report genuine NS125 engine RPM.

SN65HVD230: regulated 3.3V VCC with local 100nF transceiver bypassing,\nGND to logic ground, R/RXD to GPIO16,
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
The owner's preferred standard uses direct GPIO27 data. It does not include a
level shifter, a shifter bypass capacitor or a shifter-input pull-down resistor.
No settings need editing for six compatible WS2812B pixels. Keep CAN disconnected.

Required: ESP32 DevKit V1, six 5V WS2812B GRB/800kHz pixels, USB data cable,
330-ohm resistor, 1000uF 10V electrolytic capacitor, regulated 5V supply rated
at least 1A, secure wires/connectors and a way to check voltage/polarity.

| From | To |
| --- | --- |
| ESP32 GPIO27 (3.3V data) | 330-ohm resistor, then first LED DIN; resistor near DIN |
| External regulated +5V | Strip +5V only |
| External supply negative | Strip GND and ESP32 GND, joined |
| 1000uF 10V capacitor | Across LED +5V/GND, near strip input; + lead to +5V, striped negative to GND |
| ESP32 USB socket | Separate USB power and upload |
| Last pixel DOUT | Unconnected |
| GPIO16/17 and CAN module | Reserved, unconnected during this LED test |

Follow the printed arrow: DIN -> pixel 1 -> pixel 6 -> DOUT. Do not feed
DOUT. Verify actual pin labels instead of trusting wire colors. Keep data
wiring short, with a ground wire alongside it. Software direction reversal
changes only the displayed pattern, not the input end of the strip.

POWER AND CONNECTION ORDER
--------------------------
1. Disconnect both supplies before assembling/checking/rewiring. Join ground
   first and keep it connected. Check voltage, polarity and absence of shorts.
2. External +5V powers the LEDs only. USB powers the ESP32. Do not join external
   +5V to the USB-powered board's 5V/VIN/3V3 pins. Positive rails stay separate.
3. Apply regulated LED 5V first, then connect ESP32 USB. Do not hot-plug data.
4. Disconnect USB first, then LED power. Let the capacitor discharge before
   rewiring; do not short its leads. Remove ground last on the unpowered setup.

At a conservative 60mA/pixel, six pixels need up to 360mA. A regulated supply
rated at least 1A serves the LEDs; USB separately serves the ESP32. Brightness
40/255 is not a hardware current limiter. Never power LEDs from GPIO or 3V3.
This standalone power arrangement is not a protected automotive installation.

DIRECT-DATA COMPATIBILITY
-------------------------
REVIX LINK's standard demo sends data directly from GPIO27 to the LED strip
through a 330-ohm resistor. This works with compatible WS2812B strips, but some
5V strips require a level shifter for reliable operation.

Some strip revisions specify a logic-HIGH minimum above 3.3V. A separate 5V
supply does not correct that mismatch. Do not lower/change the LED supply to
force recognition. Check the actual strip specification and physical behavior.
If data remains unreliable with correct short wiring and stable power, a
proper 74AHCT125 buffer powered at regulated 5V is an optional remedy requiring
a correctly designed enable/bypass/ground circuit. It is not part of the
standard BOM or diagrams. No software data conversion replaces logic levels.
The actual direct-drive assembly still needs validation before reliability
claims, especially before any proposed motorcycle installation.

Wiring reference:
https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices

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
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle

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
