REVIX LINK @VERSION@ - Arduino source preview
============================================
Copyright (c) 2026 Aritra Saha. All rights reserved.

DEFAULT: ESP32 DevKit V1; six WS2812B pixels on GPIO27, GRB/800kHz,
brightness 40/255, Classic/normal, startup and smoothing enabled, red shift
flash, 150 RPM hysteresis. Synthetic start/shift/max = 1500/5800/6500 RPM.
OLED is OFF; serial diagnostics are ON at 115200 baud. No CAN connection is
needed. These values are demonstrations, not a motorcycle calibration.

Source entry point: src/main.cpp. User settings: UserConfig.h.
Keep this folder intact. REVIX_LINK.ino supplies the Arduino sketch identity;
setup()/loop() are defined once in src/main.cpp.

BUILD INSIDE UBUNTU WSL
----------------------
Use Arduino CLI in WSL with Espressif ESP32 core 2.0.17:

arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib update-index
arduino-cli lib install --no-deps "Adafruit NeoPixel@1.12.3"
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all /path/to/REVIX_LINK

Only NeoPixel is required with OLED disabled. For OLED, set
REVIX_OLED_ENABLED to 1 in UserConfig.h and install:
arduino-cli lib install --no-deps "Adafruit SSD1306@2.5.13"
arduino-cli lib install --no-deps "Adafruit GFX Library@1.11.11"
arduino-cli lib install --no-deps "Adafruit BusIO@1.17.0"

Arduino IDE 2.x can also open REVIX_LINK.ino in a Linux environment with
those same pinned dependencies. Do not install board cores or libraries on
Windows for this project.

USB UPLOAD IS A SEPARATE OWNER ACTION
-------------------------------------
First expose the USB board to WSL using your reviewed USB setup, then use
`arduino-cli board list` to discover its actual Linux port. For a standalone
USB-powered demo board only, upload the compiled sketch using:
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/YOUR_BOARD_PORT /path/to/REVIX_LINK

Do not use a guessed port or run upload on a connected motorcycle. This
package is not a validated plug-and-play motorcycle accessory.

USER SETTINGS AND CONTROLS
--------------------------
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
and recovery. Stale/invalid/unconfigured states blank the LEDs.

ISOLATED BENCH CAPTURE
----------------------
Select REVIX_USER_MODE_BENCH_LISTEN_ONLY and configure REVIX_CAN_BITRATE only
for a known bench rate. Default bitrate 0 leaves TWAI stopped. RPM mapping is
disabled; this mode logs frames and cannot report genuine NS125 engine RPM.

SN65HVD230: regulated 3.3V VCC, GND to logic ground, R/RXD to GPIO16,
D/TXD to GPIO17 with external recessive pull-up, RS held high including reset.
Inspect your module: RS may be tied low and module header positions differ
from IC pins. Do not short a grounded RS pin to 3.3V. Verify module termination.
The controller is listen-only and TX17 output is disconnected before start.
Hardware passivity still requires bench measurement. Another bench node must
acknowledge a normal transmitter; this passive device does not ACK frames.

LED HARDWARE AND POWER
----------------------
Use a suitable regulated LED supply, common logic ground, and suitable 3.3V-to-5V
data-level conversion on GPIO27. Allow for the actual strip's maximum current,
ESP32 peaks and other loads; 40/255 software brightness is not a current limit.
For six older 60mA/pixel devices, allow 360mA for pixels alone. Use local bypass
and bulk capacitors, a data resistor as required by the pixel guidance, and
100nF close to the transceiver supply pins.

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

References:
https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf
https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections
https://www.bajajauto.com/customer-service/owners-manual

Third-party libraries retain their respective notices/licenses. REVIX LINK
is governed by the included COPYRIGHT notice; no new license is granted.
