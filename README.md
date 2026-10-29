# PulseShift

Customizable ESP32 motorcycle shift-light firmware with six WS2812B LEDs, a standalone RPM demonstration and experimental receive-only CAN.

## Main features

- A repeating RPM demo with startup animation, progressive lighting and flashing shift alerts.
- Classic, Racing, Minimal and Custom styles, adjustable brightness and LED direction.
- Arduino IDE support, Wokwi simulation and passive CAN investigation tools.

**No settings need changing for the standard demo:** six LEDs, GPIO27, brightness 40/255, Classic style and OLED off. No motorcycle or CAN connection is needed.

## What you need

![Standard parts for the PulseShift build](assets/readme/hardware-overview.svg)

- ESP32 DevKit V1 and a USB **data** cable.
- One six-LED **5V WS2812B strip**, GRB / 800kHz.
- A **330Ω resistor** and **1000µF, 10V electrolytic capacitor**.
- A regulated **5V supply rated at least 1A**, secure wires and a multimeter.

The optional SN65HVD230 CAN module stays disconnected for the demo.

## How to wire it

Disconnect both supplies before assembly. Connect **GPIO27 → 330Ω → DIN**, with the resistor near the first LED. Keep the data lead short.

![Direct demo wiring with separate LED power and common ground](assets/readme/main-wiring.svg)

Connect to **DIN**, following the strip's printed arrow. Leave **DOUT** unconnected.

![LED data direction from DIN toward unused DOUT](assets/readme/led-direction.svg)

**USB powers the ESP32. External regulated 5V powers the LEDs.** Join the supply negative, LED GND and ESP32 GND. Never join external +5V to the USB-powered ESP32's 5V, VIN or 3V3 pins. Never power the strip from a GPIO.

Place the capacitor across LED +5V and GND: **positive to +5V, striped negative to GND**.

![Separate power supplies, common ground and capacitor polarity](assets/readme/power-safety.svg)

Check voltage, polarity and all connections before power-on. Power the LEDs before connecting USB; disconnect USB first when switching off. Let the capacitor discharge before rewiring. [Detailed wiring and power guidance](arduino/README.txt).

## Install with Arduino IDE

Keep the motorcycle disconnected. Use **Arduino IDE 2.x for Linux inside Ubuntu WSL2/WSLg**. Keep the project, ESP32 core and libraries inside WSL.

1. Extract the versioned Arduino ZIP into your WSL home folder. Open **PulseShift/PulseShift.ino** and keep the sketch folder intact.
2. Add the [Espressif board-manager URL](https://espressif.github.io/arduino-esp32/package_esp32_index.json) in IDE Preferences. Install **esp32 by Espressif Systems 2.0.17** through Boards Manager.
3. Install **Adafruit NeoPixel 1.12.3** through Library Manager. The default demo needs no OLED libraries.
4. Select **ESP32 Dev Module** and the board's USB port. USB access must be available inside WSL. Leave **UserConfig.h** unchanged.
5. Click **Verify**, then **Upload**. The demo starts automatically.

[Arduino IDE installation](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-downloading-and-installing/) · [WSL, Arduino CLI and Wokwi guide](arduino/README.txt)

## Download

**[Download PulseShift v0.3.0](https://github.com/iamaritrasaha/PulseShift/releases/tag/v0.3.0)**

[Arduino ZIP](https://github.com/iamaritrasaha/PulseShift/releases/download/v0.3.0/PulseShift-Arduino-v0.3.0.zip) · [SHA-256](https://github.com/iamaritrasaha/PulseShift/releases/download/v0.3.0/PulseShift-Arduino-v0.3.0.sha256) · [All releases](https://github.com/iamaritrasaha/PulseShift/releases)

For v0.3.0, download **PulseShift-Arduino-v0.3.0.zip** and **PulseShift-Arduino-v0.3.0.sha256** from Assets. Check the ZIP against its SHA-256 before extracting. Use the packaged Arduino ZIP, not GitHub's automatic source archive.

**v0.3.0 is the current verified software prerelease.** Physical motorcycle qualification remains pending.

## After upload

![Startup sweep, progressive fill, shift alert and missing-data blanking](assets/readme/expected-behavior.svg)

Expect a startup animation followed by a growing light bar and red flashing at the simulated shift point. Deliberate gaps in valid data make the LEDs go dark, then recover. **This demonstration does not measure engine RPM.**

## Configuration

Optional settings in [UserConfig.h](UserConfig.h):

- **Brightness:** PULSESHIFT_LED_BRIGHTNESS, default **40**, range 0..255. This is not a hardware current limiter.
- **Style:** PULSESHIFT_SHIFT_LIGHT_STYLE, default **Classic**.
- **Direction:** PULSESHIFT_LED_DIRECTION, default **Normal**. Reversal changes the pattern, not the DIN connection.

Rebuild and upload after edits. [Advanced settings](arduino/README.txt).

## Experimental CAN

**CAN_CAPTURE** records existing CAN frames without diagnostic requests. **VEHICLE_RPM** requires an independently verified profile and never substitutes demo RPM for missing data. GPIO16 and GPIO17 remain reserved for receiver work.

The **2025 Indian Bajaj Pulsar NS125 ABS** profile remains **UNVERIFIED**, with bitrate **0** and live decoding disabled. A scanner displaying RPM does not prove passive broadcasts or establish the diagnostic transport. [Qualified capture and offline discovery workflow](arduino/README.txt).

## Safety limitations

- Common ground, correct DIN direction and capacitor polarity are essential. Never change wiring while powered.
- PulseShift's standard demo sends data directly from GPIO27 through a 330Ω resistor. Some 5V WS2812B strips need a proper 74AHCT125 level shifter for reliable 3.3V data recognition. Separate LED power does not fix the signal-level mismatch; do not lower the rated LED supply voltage to compensate.
- Keep the CAN module disconnected until a qualified technician verifies the connector, transport, RS wiring, TXD bias, reset/unpowered passivity, termination and automotive protection. Software listen-only alone does not qualify the hardware.
- Motorcycle RPM integration and shift calibration are unverified. The demo's RPM thresholds are synthetic. This is a **software prerelease**, not a physically qualified motorcycle accessory.
- Wokwi DEMO does not validate physical CAN. The isolated TWAI test still has an unresolved transmission-completion limitation.

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
