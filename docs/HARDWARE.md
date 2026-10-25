# Hardware: ESP32 + SN65HVD230 + WS2812B

Target motorcycle: **Bajaj Pulsar NS125, 2025 ABS model**, as specified by the user. This replaces the earlier NS160 target. The existing ESP32 DevKit V1, six WS2812B LEDs and SSD1306 OLED are retained.

The bike's physical diagnostic interface, connector pinout, CAN availability, bitrate, RPM ID and encoding remain unverified. ABS alone does not establish that an accessible connector carries broadcast RPM over CAN. Bajaj's [manual catalog](https://www.bajajauto.com/customer-service/owners-manual) lists an NS125 LED BT ABS manual, but this is not a verified 2025 wiring/protocol specification. The linked current PDF could not be fetched during this update. No vehicle-specific CAN values were inferred.

## Bench wiring reference

| ESP32 / supply | Component connection |
| --- | --- |
| GPIO16 (TWAI RX) | SN65HVD230 R / RXD, chip pin 4 |
| GPIO17 (TWAI TX, output disabled in bike build) | SN65HVD230 D / TXD, chip pin 1; external recessive pull-up required |
| Regulated 3.3 V | SN65HVD230 VCC, chip pin 3 |
| Common logic ground | SN65HVD230 GND, chip pin 2; LED and display ground |
| Fixed 3.3 V for passive capture | SN65HVD230 RS, chip pin 8, after checking module wiring |
| GPIO27 through a suitable 3.3-to-5 V data buffer | WS2812B DIN |
| Suitable regulated 5 V supply | WS2812B power; size for the actual strip, not the brightness setting |
| GPIO21 / GPIO22 | SSD1306 SDA / SCL; use ESP32-safe I2C pull-ups |

Chip pin numbers are not module header positions. Check the specific module's labels/schematic. CANH (chip pin 7) and CANL (pin 6) go only to a verified bench CAN bus at this stage. No motorcycle connector pins are assigned here.

The [TI SN65HVD230 datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) specifies a 3.3 V supply. RS high disables its driver while leaving its receiver enabled. For this passive receiver, hold RS high in hardware, including during ESP32 reset. Some modules tie RS low: inspect and resolve that circuit before applying 3.3 V; do not short a grounded pin to the supply. SN65HVD231 has different standby behavior and is not interchangeable for this arrangement. Check whether the module includes a termination resistor before attaching it to any existing bus.

SN65HVD230 is a transceiver, not an SPI CAN controller. The firmware uses ESP32 TWAI directly; it needs no MCP2515/SPI library. Software cannot verify RS wiring or bus electrical passivity.

The GPIO17-to-GPIO16 wire in `diagram.json` belongs only to the Wokwi self-test. Remove that loopback for real hardware and use the transceiver. Replay ignores the simulated wire. Wokwi does not validate this transceiver or the bike wiring.

## Firmware settings

Pins, six-LED count and brightness are in `include/config.h`. LEDs use GRB, 800 kHz signaling. Brightness 40/255 and the 6500 RPM demonstration scale are UI settings, not a power limit or a verified NS125 redline/shift point. No guessed RPM is displayed in bike mode.

`esp32dev_bike` excludes the synthetic ECU and transmit method. It defaults to bitrate 0, keeping TWAI stopped. Only configure a bitrate after verification on the intended network. Configured capture uses `TWAI_MODE_LISTEN_ONLY`, logs complete received frames, and leaves RPM decoding disabled. GPIO17 output is disconnected before starting TWAI because the bundled SDK lacks the listen-only dominant-bit erratum workaround. The external RS-high arrangement adds a separate hardware transmission inhibit. See [CAN investigation](CAN-INVESTIGATION.md).

## Before bench and motorcycle use

- Bench: verify supply levels, RXD/TXD/RS behavior, grounding and termination; use a known CAN source plus an ACK-capable node/analyzer. This passive receiver does not acknowledge frames. Measure behavior during reset, startup and errors.
- Check the exact WS2812B version's power and input-level requirements; use a suitable data buffer and power supply. Never power the LEDs from an ESP32 GPIO. The [NeoPixel wiring guide](https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections) covers power, ground, data and level shifting.
- Vehicle: obtain the correct 2025 variant wiring/service information, verify the connector and signaling type, and corroborate RPM captures with an independent RPM reference. If the interface is K-line or another non-CAN interface, SN65HVD230 cannot receive it; a different interface would be required.
- Provide fused, protected automotive power conversion, reverse-polarity/transient protection, sound grounding, suitable connectors and enclosure. Never feed motorcycle battery voltage directly into ESP32 3.3 V, the transceiver, or the LED strip. Do not add termination blindly.

Frame capture uses software dequeue timestamps and a bounded serial queue at 115200 baud; loss counters must be checked. No ECU requests, diagnostic commands or spoofed frames are sent. No firmware has been flashed to the motorcycle. Bench compatibility and real RPM decoding remain to be validated.
