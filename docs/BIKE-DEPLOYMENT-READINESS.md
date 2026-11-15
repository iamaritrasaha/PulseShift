# PulseShift: motorcycle deployment preflight (15 November 2026)

**Status: software-tested only for the changes in this patch.**
Do not describe this as a fully field-verified shift light until the physical
LED, regulated power supply, and app/OTA tests are completed.

## Known-working pieces

- ESP32 DevKit V1 with Silicon Labs CP210x serial adapter detected on COM5.
- Existing production firmware booted on the ESP32 and reported a running
  TWAI controller at 500000 bit/s, mounted LittleFS, BLE advertising, and
  `app0` OTA image state `UNDEFINED` (normal for a USB-installed image).
- Previously captured 500-kbit/s motorcycle CAN traffic successfully.
- User independently backed up **23370 frames**, **467476 bytes**, all valid
  recorded CRCs, to a verified CSV without erasing flash.
- The decoder defaults remain standard ID `0x301`, byte index 0, x40.0,
  zero offset. Field correlation near 3000 RPM supports these defaults but
  a discrepancy at idle remains. See `NS125-RPM-FIELD-EVIDENCE.md`.
- The ESP32 can boot, receive CAN and render the provisional RPM bar without
  a phone, USB connection, laptop or internet, **once powered properly**.
  This does not make the display an independently calibrated shift cue.

## Changes specific to field operation

- When a diagnostic log reaches the 128 KiB quota and no previous archive
  exists, the firmware renames the old log to `/diag-prev.log` instead of
  overwriting it. New `/diag.log` then records subsequent field events.
  Existing CAN captures and NVS credentials are unchanged.
- `LOGS` exports both archive and current diagnostic logs over USB serial.
  Do not run it while the motorcycle is running: long serial exports may
  temporarily delay other event processing.
- If an archive already exists, the firmware never deletes or overwrites it.
  When both slots are full, the logger honestly reports `FULL` and stops
  writing; old logs remain readable. More field sessions require exporting
  logs and deliberate user-led cleanup.
- A BOOT-button long press cannot falsely resume a recording when an older
  capture is preserved, storage is unavailable, or a write error is latched.
  The same fail-closed rule applies to serial and BLE recording controls.

## Critical: current saved capture blocks a new recording

The 467476-byte CAN capture is still preserved in the ESP32's flash.
While it is preserved, new CAN frames can be *decoded live* for telemetry
and the provisional LED preview, but **new raw CAN frames are not saved**.
This is deliberate data protection, not a broken TWAI driver.

If you need a new raw field recording, after independently verifying
the exported backup, connect the ESP32 to the PC before installation and
type `CLEAR YES` in the 115200-baud serial monitor. **It erases the current
on-device CAN capture and current diagnostic log.** A prior archived log
is not deleted. Do not use this command if you need the unsaved originals.
`DUMP STATUS` and `STATUS` are non-destructive alternatives.

If you only need the live provisional RPM preview and do not require another
raw recording, you can leave the capture preserved.

## Physical installation gate: check with hardware in hand

1. The bike has switched 12V, CAN-H, CAN-L, and ground. **CAN-H/CAN-L/GND
   alone cannot power the ESP32 or LEDs.** Do not connect any motorcycle 12V
   directly to ESP32 VIN, 3V3 or WS2812B strip.
2. Use a suitably fused, automotive-rated DC/DC converter, with regulated
   5V output and an appropriate common ground. Validate the actual
   converter's automotive voltage transient and current ratings first.
3. ESP32 uses GPIO16 as transceiver RX (CRX/RXD). ESP32 GPIO17 stays
   **physically disconnected**. The SN65HVD230 TXD (often CTX) is held
   recessive at 3V3. The firmware cannot verify this physical wiring.
4. Verify breakout Rs pin configuration for receive operation, and verify
   there is no additional 120-ohm termination resistor across the already
   terminated motorcycle CAN bus. Check unpowered connections before use.
5. Six WS2812B LEDs require regulated power, common ground, and a verified
   data path from GPIO27. With the LEDs unavailable, their operation is
   **NOT TESTED**. Do not rely on an untested display while riding.
6. With no Android app available, BLE owner authorization and signed OTA
   rollback remain **NOT TESTED**. Perform future firmware updates by USB
   on a safe stationary bench. Never flash while the motorcycle CAN bus is
   connected or active.
7. Before permanent mounting, check enclosure, strain relief, water ingress,
   vibrations, heat, ignition power-off behavior and current draw. The
   electronics must not interfere with steering, braking or vehicle wiring.

## Read-only serial preflight (USB bench)

At 115200 baud:

```text
STATUS
CONFIG STATUS
RPM STATUS
DUMP STATUS
OTA STATUS
LOGS
```

`LOGS` can produce considerable text; capture it to a PC file and keep it
private. On a USB-installed application `OTA STATUS` can report
`UNDEFINED`, which is not by itself an error. For an *OTA-installed*
candidate, confirmation and rollback still require physical testing.

**Physical tests still pending:** LED strip output; regulated motorcycle
power behavior; first-run CAN acquisition on the final wiring harness;
Android owner auth and signed OTA rollback; vibration and thermal behavior.
