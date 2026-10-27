# REVIX LINK v0.2.0 | Arduino IDE software prerelease

This is an Arduino IDE software prerelease for standalone demonstration and
controlled development. It is not a stable or plug-and-play motorcycle release.
The intended 2025 Indian Bajaj Pulsar NS125 ABS remains an unverified target.

## What is included

- **DEMO**, the unchanged default: six LEDs, startup animation, progressive fill,
  shift alerts with hysteresis, Classic/Racing/Minimal/Custom styles, smoothing,
  direction/brightness options and serial controls. Its 20-second synthetic
  sequence includes complete CAN frames, malformed data, gaps and recovery.
- **CAN_CAPTURE**: receive-only TWAI, GPIO16 RX, GPIO17 output disconnected before
  start, bounded raw-frame/status logging and explicit driver/log-loss counters.
  Listen-only operation sends no application frames, diagnostic requests or ECU
  commands. Bitrate defaults to 0, keeping the controller stopped.
- **VEHICLE_RPM**: validated frames, expiring RPM state and the existing LED engine.
  Explicit profiles support bounded unsigned fields, byte order, scale/offset,
  valid ranges, timeouts and optional validity/mux/counter/checksum rules.
  Proprietary rules require an explicit reviewed validator.
- The NS125 profile is **UNVERIFIED**, with no supplied bitrate or RPM mapping.
  Vehicle mode stays unconfigured; missing data never falls back to demo RPM.
- Offline capture investigation ranks candidate fields against independent
  timestamped RPM references and exports UNVERIFIED vectors. It never enables
  a firmware profile. Included recordings and signal IDs are synthetic fixtures.
- The optional OLED RPM bar now shares the LED engine's calibrated maximum,
  including verified vehicle calibration, instead of the synthetic demo limit.
- A reproducible Arduino source ZIP, beginner visual guide, five editable SVGs,
  WSL/PlatformIO setup and separate isolated TWAI investigation sources.

## Standard standalone hardware

ESP32 DevKit V1, one continuous six-pixel 5V WS2812B strip (GRB, 800kHz),
USB data cable, 330-ohm data resistor, 1000uF 10V bulk capacitor, secure wires
and a separate regulated 5V supply rated at least 1A.

Wire GPIO27 through 330 ohms to **DIN**, keep the lead short and leave DOUT unused.
Join supply negative, LED ground and ESP32 ground. Check capacitor polarity.
USB powers the ESP32 separately; never join external +5V to its 5V/VIN/3V3 pins.
Assemble and check wiring with both supplies disconnected. Keep CAN disconnected
during the LED demonstration.

Defaults remain six pixels, brightness 40/255, Classic/normal, OLED disabled and
standalone DEMO. No settings need editing for the standard compatible strip.
A level shifter is not part of the standard build. Separate 5V LED power does
not correct a 3.3V data-level mismatch. Some strips require an optional properly
designed 74AHCT125 buffer; do not change strip supply voltage to force recognition.

## Verification and known limitations

The candidate is checked in Ubuntu WSL using ESP32 Arduino core **2.0.17**,
pinned libraries, all four PlatformIO targets and **15 extracted Arduino
configurations**, including optional OLED and receive-only variants. Verification
includes package integrity/reproducibility, compile-time rejection, linked-binary
mode isolation and native tests with warnings as errors, ASan and UBSan.

The native suite preserves 33 cases / 638788 assertions, 7186 vehicle checks,
162 driver checks per receive-only mode, 202 OLED calibration assertions and
12 CAN-tool/configuration Python tests. Additional release-guard tests validate
version-specific notes, asset checksums and publication failure behavior without
contacting GitHub.

The recorded Wokwi replay demo passed **24 runtime checks**. This tests complete
synthetic frames, decoding, state, serial controls and dashboard behavior.
It does not validate the TWAI controller, a physical bus or electrical levels.

**Wokwi TWAI transmission completion remains unverified.** The isolated self-test
receives one frame but leaves TX pending, reports no completion and halts once.
Four minimal probes reproduce this failure; a disconnected-loopback control
still self-receives a frame. The test remains enabled. No fake TX success or
recovery loop is used. See the [investigation and recorded evidence](https://github.com/iamaritrasaha/REVIX-LINK/blob/v0.2.0/test/TWAI_WOKWI.md).

Motorcycle RPM integration has not been physically qualified.
The actual SN65HVD230 transceiver's electrical passivity has not been physically qualified.
Direct 3.3V LED signal compatibility with the actual 5V strip has not been physically qualified.

Vehicle connector pinout, variant, bitrate, passive RPM encoding, signal timing,
checksums/counters and shift calibration require corroborated measurements.
The actual breakout's RS wiring, recessive TXD bias, termination, reset/power
behavior and automotive protection require electrical inspection. A six-pin
module and software listen-only mode do not establish vehicle safety. Do not
add termination to an established network without verification. No motorcycle
connection or physical firmware upload is performed by this software release.

## Arduino IDE installation

Download **REVIX-LINK-Arduino-v0.2.0.zip** and its **.sha256** asset. Verify the
checksum, extract under Ubuntu WSL, keep the REVIX_LINK folder intact and open
REVIX_LINK.ino in Arduino IDE 2.x for Linux/WSLg. Install ESP32 by Espressif Systems
2.0.17 and Adafruit NeoPixel 1.12.3. OLED-disabled defaults need no display library.
The packaged README.txt covers the pinned optional OLED libraries, exact wiring,
three modes and WSL CLI builds. Board upload is a separate owner action with
the motorcycle disconnected.

Copyright (c) 2026 Aritra Saha. All rights reserved. COPYRIGHT is unchanged.
