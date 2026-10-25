REVIX LINK Arduino IDE package @VERSION@
=======================================

Copyright (c) 2026 Aritra Saha. All rights reserved.

This package targets an ESP32 DevKit V1. Its default mode is a standalone
demonstration. Synthetic frames exercise the existing decoder and six LEDs.
The RPM values are simulated and do not describe the 2025 Bajaj Pulsar NS125
ABS. The sketch does not transmit CAN frames in its default mode.

Requirements
------------

- Arduino IDE 2.x
- ESP32 by Espressif Systems, version 2.0.17
- Adafruit NeoPixel, version 1.12.3
- For optional OLED diagnostics only: Adafruit SSD1306 2.5.13, Adafruit GFX
  Library 1.11.11, and Adafruit BusIO 1.17.0

Install the libraries from Arduino IDE's Library Manager. Select each named
version from the version selector when installing. OLED is disabled by default,
so only Adafruit NeoPixel is required for the default build.

Install and upload
------------------

1. Extract this ZIP. Keep the REVIX_LINK folder and its contents together.
2. In Arduino IDE 2.x, open REVIX_LINK/REVIX_LINK.ino.
3. Open File > Preferences. Add this URL to Additional Boards Manager URLs:
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
4. Open Tools > Board > Boards Manager, search for esp32, and install version
   2.0.17 of ESP32 by Espressif Systems.
5. Install the libraries listed above from Library Manager.
6. Edit UserConfig.h for LED pins, count, brightness, serial diagnostics, or
   optional OLED diagnostics. The defaults are for the ESP32 DevKit V1.
7. Select Tools > Board > esp32 > ESP32 Dev Module.
8. Connect the board by USB and select its port in Tools > Port.
9. Click Verify, then Upload.
10. Open Serial Monitor at 115200 baud to see simulated RPM and status logs.

Optional OLED
-------------

Set REVIX_OLED_ENABLED to 1 in UserConfig.h and install the three OLED libraries
listed above. The default LED-only build does not compile or link OLED code.

Receive-only bench mode
-----------------------

This mode is for an isolated, verified bench setup only. It does not decode an
NS125 RPM mapping. To select it, change REVIX_OPERATING_MODE to
REVIX_USER_MODE_BENCH_LISTEN_ONLY in UserConfig.h. The default CAN bitrate is 0,
which keeps TWAI stopped. Set a supported nonzero bitrate only after verifying
it on an isolated bench.

The ESP32 TX GPIO is configured as an input in this mode. The SN65HVD230 must
also be held in its hardware silent state by keeping RS high, and its TXD input
must remain recessive with an external pull-up. Software settings alone do not
prove electrical passivity. Do not connect this unverified prototype directly
to the motorcycle.

Limitations
-----------

The motorcycle CAN bitrate, message mapping, wiring, and RPM decoding remain
unverified. No actual NS125 RPM readings are claimed. The default demo only
exercises the software path with generated frames.

Third-party libraries retain their own licenses. REVIX LINK source is covered
by the included COPYRIGHT notice.
