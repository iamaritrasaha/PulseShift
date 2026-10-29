PulseShift @VERSION@ - Arduino source preview
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
Keep this folder intact. PulseShift.ino supplies the Arduino sketch identity;
setup()/loop() are defined once in src/main.cpp.

BUILD INSIDE UBUNTU WSL
----------------------
Extract the ZIP under your WSL home directory, not /mnt/c or /mnt/d.
Open a WSL terminal in the extracted PulseShift folder. Use Arduino CLI
1.5.1 with Espressif ESP32 core 2.0.17. Skip installation if already present.

To install the CLI in WSL (requires curl):
mkdir -p "$HOME/.local/bin"
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh -o /tmp/pulseshift-arduino-cli-install.sh
BINDIR="$HOME/.local/bin" sh /tmp/pulseshift-arduino-cli-install.sh 1.5.1
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
PULSESHIFT_OLED_ENABLED to 1 in UserConfig.h and install:
arduino-cli lib install --no-deps "Adafruit SSD1306@2.5.13"
arduino-cli lib install --no-deps "Adafruit GFX Library@1.11.11"
arduino-cli lib install --no-deps "Adafruit BusIO@1.17.0"

Arduino IDE 2.x can also run inside WSL2/WSLg. Open PulseShift.ino, install
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
| Different LED wiring or count | PULSESHIFT_LED_DATA_GPIO, PULSESHIFT_LED_COUNT (1..32); check wiring and current budget; reserve GPIO16/17 |
| Personal appearance | PULSESHIFT_LED_BRIGHTNESS (0..255), PULSESHIFT_SHIFT_LIGHT_STYLE (Classic/Racing/Minimal/Custom), PULSESHIFT_CUSTOM_COLORS (RGB triples, channels 0..255), PULSESHIFT_LED_DIRECTION (normal/reversed) |
| Animation or alert preference | PULSESHIFT_STARTUP_ANIMATION_ENABLED, PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED (0/1), PULSESHIFT_SHIFT_ALERT_MODE (off/solid red/flashing red/flashing white), PULSESHIFT_ANIMATION_INTERVAL_MS (5..250ms) |
| Synthetic scale only | PULSESHIFT_DEMO_START_RPM, PULSESHIFT_DEMO_SHIFT_RPM, PULSESHIFT_DEMO_MAX_RPM (maximum 1..20000; start < shift <= maximum), PULSESHIFT_ALERT_HYSTERESIS_RPM (0 to less than shift minus start) |
| Optional idle effect | PULSESHIFT_IDLE_EFFECT_ENABLED (0/1), PULSESHIFT_IDLE_MAX_RPM (0..shift RPM) |
| Add an OLED | PULSESHIFT_OLED_ENABLED (0/1), PULSESHIFT_OLED_SDA_GPIO, PULSESHIFT_OLED_SCL_GPIO; use a 128x64 SSD1306 at 0x3C, safe 3.3V pull-ups and the display libraries |
| Serial preference | PULSESHIFT_SERIAL_DIAGNOSTICS (0/1), PULSESHIFT_SERIAL_BAUD (1..2000000); match the serial monitor rate; diagnostics off also removes runtime demo commands |
| Verified isolated CAN bench only | PULSESHIFT_OPERATING_MODE, PULSESHIFT_CAN_RX_GPIO, PULSESHIFT_CAN_TX_GPIO, PULSESHIFT_CAN_BITRATE; read the bench requirements before changing them |

GRB / 800kHz is fixed in the existing LED driver, not a UserConfig.h option. A different RGB order, RGBW strip or protocol requires a reviewed driver change. Do not change the RGB order of palette triples to compensate. CAN bitrate does not select a motorcycle protocol; no verified vehicle mapping is supplied.


FIRMWARE MODES
---------------
DEMO: PULSESHIFT_USER_MODE_DEMO is the default. It needs no CAN module. Synthetic
complete CAN frames pass through the common decoder and expiring RPM state into
the existing ShiftLightEngine. Demo thresholds are not motorcycle calibration.

CAN_CAPTURE: PULSESHIFT_USER_MODE_CAN_CAPTURE logs raw passive CAN frames only.
Set a verified isolated bench bitrate explicitly; 0 leaves TWAI stopped.
No transmit, ACK, diagnostic request or ECU command is added. Serial records
include timestamp, ID, standard/extended format, RTR, DLC, data and decode status.
Processing and logging are bounded; inspect driver overflow and dropped logs.

VEHICLE_RPM: PULSESHIFT_USER_MODE_VEHICLE_RPM uses the same receive-only transport,
profile decoder, RPM state and LED engine. The selected NS125 profile in
src/vehicle_profile.cpp remains UNVERIFIED with no bitrate or signal mapping.
It cannot produce guessed vehicle RPM or fall back to the demo. A VERIFIED
profile needs corroborated captures, independent RPM references, reviewed
signal/checksum/counter rules, electrical qualification and shift calibration.
Invalid/stale/faulted RPM blanks the LEDs. Unrelated traffic cannot refresh it.
The optional OLED bar uses the same calibrated maximum as the LED engine.

Advanced profiles live in src/vehicle_profile.h/.cpp; keep bitfields out of
beginner settings. Unsigned LE LSB0 and BE MSB0 stream fields are supported;
BE start bits are not DBC Motorola numbering. Protocol-specific proprietary
rules require an explicitly reviewed validator. Simple Sum8/Xor8 checksums
are not a universal vehicle checksum implementation.

In the repository, tools/can_investigate.py reads raw logs and an independent
timestamp_ms,rpm CSV, reports frequencies/changing fields, ranks numerical
candidates and exports UNVERIFIED review vectors. It never enables firmware
profiles. Clock alignment, counters, multiplexing, checksums and false
correlations need independent review. The analysis tools and synthetic fixtures
are repository development tools, not additional Arduino dependencies.

Motorcycle RPM integration, the actual transceiver's electrical passivity and
direct 3.3V LED compatibility have not been physically qualified.

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
shows PulseShift, operating mode, validated RPM or --, CAN status and a bar.
The 20-second automated cycle includes malformed/out-of-range frames, silence
and recovery, with or without an OLED. After the startup sweep, the LEDs show
progression and shift flashing, then go dark when valid RPM becomes stale, and
resume when valid frames return. The deliberate dark interval is expected.

ISOLATED BENCH CAPTURE
----------------------
Select PULSESHIFT_USER_MODE_CAN_CAPTURE and configure PULSESHIFT_CAN_BITRATE only
for a known bench rate. Default bitrate 0 leaves TWAI stopped. RPM mapping is
disabled; this mode logs frames and cannot report genuine NS125 engine RPM.

SN65HVD230: regulated 3.3V VCC with local 100nF transceiver bypassing,
GND to logic ground, R/RXD to GPIO16,
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
PulseShift's standard demo sends data directly from GPIO27 to the LED strip
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
Open /home/hrik/projects/PulseShift in VS Code through Remote WSL.
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
disabled. The optional VS Code task "PulseShift: Record bench CAN log" captures
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

Third-party libraries retain their respective notices/licenses. PulseShift
is governed by the included COPYRIGHT notice; no new license is granted.


AUDIT SAFEGUARDS FOR ADVANCED SETTINGS
With Serial diagnostics enabled, UART0 GPIO1 and GPIO3 are reserved. LED,
reserved CAN pins and enabled OLED pins cannot share them. Default pins do not
change. Disabling diagnostics does not prove a custom pin assignment safe:
the DevKit USB-UART hardware and its wiring must still be checked.

Capture analysis expects one boot/session per log file. A PulseShift or legacy
REVIX LINK startup banner after received frames is rejected, including near timer rollover.
Split recordings at each reboot. A rollover without a restart marker is accepted;
a missing marker cannot reliably distinguish reboot from rollover. --max-frames
counts all RX records, including malformed records. Lines over 4096 characters
are rejected. These limits prevent malformed inputs from bypassing the budget.


NAME MIGRATION
PulseShift v0.3.0 is the proposed first renamed software prerelease.
Published v0.1.0 through v0.2.1 retain their REVIX-LINK-Arduino filenames and
REVIX_LINK/REVIX_LINK.ino sketch layout. Never rename or overwrite their assets.

New settings use PULSESHIFT_ with the same values and defaults. Saved v0.2.1
UserConfig.h files and legacy build defines remain supported by ConfigCompat.h.
Keep ConfigCompat.h beside UserConfig.h when using a saved configuration.
If both prefixes define a setting, values must agree. For custom palettes use
one prefix, or identical entries. Prefer the new UserConfig.h and copy your
settings into it. New capture logs start with PulseShift; old REVIX LINK logs
retain their reboot/session-boundary protection.

Passive broadcast RPM discovery (advanced, not motorcycle installation approval)
-----------------------------------------------------------------------------
PulseShift only listens to existing CAN traffic. It sends no OBD/UDS requests,
scanner commands or ECU messages. The owner reports that a diagnostic scanner
showed RPM immediately on the 2025 Indian NS125 ABS. This does not prove passive
broadcasting or independently establish the scanner's physical transport.

A qualified technician must first establish the connector, transport and bitrate
from vehicle-specific evidence. Never guess pins or probe bitrates on the bike.
Qualify the actual SN65HVD230 board: RS access/wiring and hardware driver-disable,
TXD recessive bias, 3.3V supply, reset/unpowered behavior, common-mode limits,
termination, protected automotive power and transient protection. A six-pin
breakout may hide RS. Software listen-only and GPIO17 input isolation do not
qualify it. Do not add 120 ohms to an established bus without network evidence.
Actual motorcycle wiring remains blocked until these checks pass.

After qualification, record separate passive sessions with the scanner absent:
ignition on/engine off, idle, several independently measured stable speeds,
return to idle and engine off. Include repeated rises and falls. Use independent
RPM references and documented clock alignment. Operate safely under technician
control. A scanner attached during capture can cause requested responses that
are not broadcast evidence. Keep each power cycle/recording in its own log and
CSV. Never concatenate sessions, including those with apparently increasing time.

Logs use RX t=<dequeue ms> id=<hex> ext=<0/1> rtr=<0/1> dlc=<0..8>
data=<hex> <decoder result>. Preserve the startup banner and CAN/APP records:
controller state, TEC/REC, bus errors, missed/overrun counts and logDropped.
Timestamp is dequeue time, not hardware arrival time. Capture uses a 64-frame
receive queue, at most 16 reads per application iteration and a bounded UART
queue at 115200 baud. High traffic can lose records. Bus-off/overflow/status
faults stop reception; isolated bus-error alerts are counted. No automatic
recovery, bitrate scan or simulated fallback runs in receiver modes. Bitrate
zero disables reception. Restart only after the fault is investigated.

Run offline commands from the Git repository root in WSL. The upload ZIP does
not include tools/ or test/; use the repository checkout for investigation.
Offline example, entirely synthetic fixture data:
python3 tools/can_investigate.py test/fixtures/synthetic_capture.log \
  --reference test/fixtures/synthetic_rpm.csv --max-skew-ms 30 \
  --output .local-notes/candidate-review

Reference CSV header: timestamp_ms,rpm. Timestamp offset is ADDED to reference
times. Set --reference-offset-ms from an independent synchronization event.
For real separate recordings, append one or more:
  --validation heldout.log heldout.csv 0
The last value is that session's documented reference offset in milliseconds.
Do not tune offsets by maximizing correlation. Use --max-frames, --top and
--max-vectors-per-candidate to bound analysis (at most 16 validation sessions).

The analyzer searches unsigned byte-aligned 8/16/24/32-bit fields in both byte
orders. Training samples choose scale/offset; chronological validation ranks
fields. Independent files score frozen candidates without refitting/reranking.
Duplicate paths/content are rejected; different files alone cannot prove actual
independence. Reports include frequency/gaps, changing masks, counter-like bytes,
small-domain selector hints, seed-zero sum/XOR hypotheses, alignment/missing
samples and holdout errors. These are UNVERIFIED hypotheses. Arbitrary bitfields,
signed/compressed fields and proprietary checksums need explicit expert review.
Consider throttle, speed, load and counters as alternative explanations. A good
fit or zero error never proves RPM, validity, checksum rules or electrical safety.

Review engine-off/idle and multiple speed ranges across unseen sessions before
preparing a profile. Review raw payloads, losses and false correlations. Candidate
JSON/regression vectors never write firmware configuration. Keep detailed review
reports local. No NS125 rate, ID, signal or calibration has been supplied.

VehicleProfile contains format/ID/DLC, bounded bit extraction, scale/offset,
validity/multiplexer rules, optional Sum8/Xor8/counter or reviewed custom rule,
RPM range, timeout and separate verified shift calibration. In addition to a
VERIFIED status and reviewed evidence reference, all ProfileEvidence gates must
be true: repeatedCaptures, independentRpm, electricalQualification,
shiftCalibrationVerified, passiveBroadcastVerified, independentHoldoutVerified.
The last two gates are new; old aggregate initializers omit them and safely
remain disabled until reviewed. Do not set evidence flags merely to enable it.
The selected NS125 profile stays UNVERIFIED with bitrate zero and no mapping.
Verified frames pass through the existing RpmState and ShiftLightEngine. Invalid,
stale, missing-profile or fault states keep LEDs dark with no demo substitution.
Demo thresholds 1500/5800/6500 RPM are synthetic, not NS125 specifications.

References: Espressif TWAI listen-only documentation and TI transceiver datasheet:
https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32/api-reference/peripherals/can.html
https://www.ti.com/lit/gpn/SN65HVD230
