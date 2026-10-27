# REVIX LINK

Six ESP32-controlled LEDs show simulated engine speed and flash at the shift point.

## What this does

**No settings need changing** for six compatible WS2812B LEDs: GPIO27, brightness 40/255, Classic colors and OLED off. The repeating demo needs no motorcycle, CAN module or screen.

**[Download the current release: v0.1.0 Arduino Preview](https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.1.0)**

Choose **REVIX-LINK-Arduino-v0.1.0.zip** under Assets, not the GitHub source-code ZIP. Its bundled guide predates this direct-drive wiring. Follow the wiring below; motorcycle use remains unverified.

## What you need

![Standard parts for the standard REVIX LINK build](assets/readme/hardware-overview.svg)

- ESP32 DevKit V1 and a USB **data** cable.
- One six-LED **5V WS2812B strip**, GRB / 800kHz.
- One **330Ω** series resistor.
- One **1000µF, 10V** electrolytic capacitor.
- Regulated **5V supply rated at least 1A**, secure wires and a breadboard or soldered board.
- A multimeter to check voltage and polarity.

The optional SN65HVD230 is **not needed or connected** for this demo.

## How to wire it

With both supplies disconnected, wire **GPIO27 → 330Ω → DIN**. Keep data wiring short and join all grounds.

![Direct demo wiring: GPIO27 through 330 ohms to DIN, separate 5V LED power, bulk capacitor and common ground](assets/readme/main-wiring.svg)

Connect the signal to **DIN**, following the strip's printed arrow. Leave **DOUT** empty. Put the 330Ω resistor close to the first LED.

![LED data enters DIN and follows the arrow toward unused DOUT](assets/readme/led-direction.svg)

**USB powers the ESP32. External 5V powers only the LEDs.** Share ground, but never join external +5V to the USB-powered ESP32's 5V, VIN or 3V3 pins. Never power the strip from a GPIO.

![Separate USB and external 5V supplies, common ground, capacitor polarity and safe power order](assets/readme/power-safety.svg)

Check voltage and polarity first. Power the LEDs before connecting USB; disconnect USB first when switching off. Let the capacitor discharge before rewiring. Tap diagrams to enlarge them. [Detailed wiring](arduino/README.txt).

## How to install it

Keep the motorcycle disconnected. Use **Arduino IDE 2.x for Linux inside Ubuntu WSL2/WSLg**. Keep the project, board core and libraries inside WSL, not Windows.

1. Download and extract the release ZIP into your WSL home folder. Keep the `REVIX_LINK` folder intact and open `REVIX_LINK.ino`.
2. In IDE Preferences, add the [Espressif board-manager URL](https://espressif.github.io/arduino-esp32/package_esp32_index.json) to **Additional Boards Manager URLs**. In Boards Manager, install **esp32 by Espressif Systems 2.0.17**.
3. In Library Manager, install **Adafruit NeoPixel 1.12.3**. The default demo does not need OLED libraries.
4. Select **Tools → Board → ESP32 Arduino → ESP32 Dev Module**. Leave `UserConfig.h` unchanged.
5. Follow the power order above, then select your board under **Tools → Port**. USB must be available inside WSL; if no port appears, resolve that before continuing.
6. Click **Verify**, then **Upload**. After upload, the demonstration starts automatically.

Need the Linux IDE or command-line setup? See [Arduino's IDE installation guide](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-downloading-and-installing/) and the [included WSL guide](arduino/README.txt). Repository development and Wokwi instructions are also in that guide.

## What happens after upload

![Startup sweep, progressive Classic fill, flashing red shift alert and planned dark interval with recovery](assets/readme/expected-behavior.svg)

Expect a short startup animation, then a growing light bar. All six LEDs flash red at the simulated shift point. The test also deliberately stops valid data, so a dark interval followed by recovery is normal. It is not measuring real engine RPM.

## Common mistakes

- **Connecting DOUT instead of DIN.** Follow the printed arrow into the first LED.
- **Reversing the large capacitor.** Positive goes to +5V; the striped negative side goes to GND.
- **Forgetting common ground.** Join ESP32, strip and supply negative.
- **Joining the two positive supplies.** Keep external +5V away from the USB-powered ESP32's power pins.
- **Using long data leads.** Keep GPIO27-to-DIN wiring short.
- **Changing wires while powered.** Disconnect both supplies first; check wiring and power before blaming the code.

## What you can change

These are optional. The standard hardware works with the defaults in [UserConfig.h](UserConfig.h).

- **Brightness:** `REVIX_LED_BRIGHTNESS`, default **40**, range 0..255. Brightness is not a hardware current limiter.
- **Style:** `REVIX_SHIFT_LIGHT_STYLE`, default **Classic**; Racing, Minimal and Custom are also available.
- **LED direction:** `REVIX_LED_DIRECTION`, default **Normal**. Reversed changes the light pattern, not which end is DIN.

Rebuild and upload after changing a setting. [Advanced options](arduino/README.txt) stay in the detailed guide.

## Important note

REVIX LINK's standard demo sends data directly from GPIO27 to the LED strip through a 330Ω resistor. This works with compatible WS2812B strips, but some 5V strips require a level shifter for reliable operation.

Separate LED power does not change the 3.3V data level. Keep the LED supply at its rated 5V. If testing shows unreliable data recognition, an optional proper 74AHCT125 buffer can be added.

**The default demo works without CAN.** GPIO16 and GPIO17 remain reserved for receiver work. Advanced **CAN_CAPTURE** and **VEHICLE_RPM** modes are available in the [detailed guide](arduino/README.txt). Capture logs frames; vehicle mode requires a qualified profile. The NS125 profile is UNVERIFIED, its bitrate is 0, and its decoding stays disabled.

**2025 Indian Bajaj Pulsar NS125 ABS plug-and-play RPM integration remains unverified.** Keep SN65HVD230 disconnected until its RS wiring, termination and electrical passivity are checked. The detailed guide covers the separate isolated CAN bench setup.

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
