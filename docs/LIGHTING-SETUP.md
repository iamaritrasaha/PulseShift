# Strip setup, RPM mapping and ambient animation

## Strip and RPM

The installed count is the owner's configuration for the next boot. The running count is the number of pixels configured in the current RMT driver. A verified save reports the stored installed count separately. The renderer uses the smaller of the running and installed counts as its ceiling, so a pending shrink can darken surplus pixels immediately. The driver still sends a complete frame for its running length. When all installed LEDs are enabled, the active count follows that ceiling. An explicit active count is limited to the same ceiling.

Logical LED 1 starts at the GPIO27 data-input end in Normal direction. Reverse starts at the far end of the running strip. All layers leave pixels outside the active segment dark, including shift warnings and animations. RGB palette selection uses the existing six color zones spread over the active segment. The zones have no role in calculating configurable RPM thresholds.

- Automatic maps the first and last active LEDs to the configured endpoints. Each intermediate threshold is the integer result of `min + ((max - min) * i + (n - 1) / 2) / (n - 1)`. A single pixel uses the minimum.
- Grouped accepts one or more positive group sizes whose sum equals the configured active count. For 12 pixels, group sizes 3/3/2/2/1/1 activate 3, 6, 8, 10, 11 and 12 pixels at the six respective thresholds.
- Individual accepts a threshold for each active LED. Equal thresholds activate together. All thresholds use whole RPM, are nondecreasing, and are bounded by warning RPM. Warning RPM may equal the highest threshold. When enabled, the warning then takes precedence over the bar.
- A lit pixel releases strictly below `max(0, threshold - hysteresis)`. The warning has its own latch using the same hysteresis and uses the existing color and flash, strobe, solid or off settings. A zero-RPM sample clears the latches. An invalid, stale or nonqualifying sample also clears live RPM latches. Test and real RPM have separate latch histories.

The Android phone preview simulates the draft thresholds and warning phases, including direction, brightness, active count and hysteresis. The optional controller bench test uses synthetic RPM for ten seconds with the controller's current mapping. The CAN decoder and separate calibration rules still control which live RPM indications qualify.

## Animation failure reproduced on 2.4.0

The baseline used startup-duration bit 15 for continuous looping. Its pattern code already used modulo, and a configured loop had no total runtime limit. The renderer nevertheless used the same startup path for both purposes. With effect 5 selected, a persisted loop and zero CAN samples, `sampleQualified` was false, `rpmPriority` was true, the startup branch was skipped, and the next branch drew `SafeguardBlank`. Feeding fresh zero RPM re-entered the loop at the original boot clock phase. Letting the sample go stale immediately returned the strip to its blank safeguard. The same source path blocked a loop during active RPM and while a verified mode lacked its required calibration match. A host reproduction against merge commit `85fe474` verified missing → blank, fresh zero → animation, stale → blank. It also verified that 12 pixels lit in pairs under the first two RPM pixels' shared threshold.

The old completion latch could be set by a bench preview, but its loop conditional bypassed the latch. Changing animation ID or encoded duration restarted the same startup clock and could replay the one-shot after boot. The last phases of Sweep, Center Out and Strobe can look steady within a cycle. Their phases did wrap whenever the renderer was eligible. Android 1.2.0 preserved the old bit on the wire and queued saves in order, but its Startup panel mirrored configuration via local fields and a readback effect. A controller readback could select an older displayed value during edits. The replacement uses a single authoritative readback plus an explicitly unsaved form and sends the two animations together.

## Renderer priority and timing

1. A halted or unhealthy CAN controller displays the existing red fault blink.
2. Physical button acknowledgement flashes white, including with master power off.
3. Master lighting power off blanks all pixels.
4. An authorized synthetic RPM bench test has its existing finite lifetime.
5. Qualified running RPM in an RPM display mode preempts boot and ambient immediately. The verified shift-light effect still requires a matching calibration record. The provisional mode remains diagnostic.
6. Physical commissioning may pulse cyan while no engine-running evidence is outstanding. When running RPM has been observed, missing RPM preserves the safeguard.
7. In an RPM mode, invalid or unqualified RPM after CAN evidence shows the existing blank safeguard. CAN evidence remains latched across BLE disconnect and decoder refresh until reboot.
8. A requested animation preview runs once if idle and eligible. A higher layer cancels it.
9. The configured boot animation plays only during its initial time window. Running RPM and an authorized bench test end it early.
10. Ambient runs continuously after boot when power is on, CAN hardware is healthy, and either the controller has seen no CAN traffic since boot or fresh, valid RPM confirms zero. In verified RPM mode a zero reading must also satisfy calibration qualification. Boot, commissioning, feedback and owner previews may temporarily occupy the strip. A stopped sample becoming stale removes idle eligibility even if the last value was zero.
11. A manual decorative effect remains available in its own mode. In RPM modes without valid RPM and without eligible ambient, the strip displays its RPM safeguard.

Ambient starts a new cycle on entry after preemption. The cycle is advanced by unsigned elapsed time accumulated modulo the duration. This preserves phase across `millis()` rollover and avoids phase drift or a 10-second lifetime. The five patterns are exercised in virtual time through 10 minutes. The controller owns timing without BLE or an open Android app.

## Migration and persistence

Schema 5 is stored at `ps_cfg/blob5`. It retains the original config fields and adds the versioned lighting setup bytes under one CRC-protected NVS record. Existing `blob4` and `blob` records remain readable and are left in place. The historical six thresholds and historical pixel assignment `floor(i*6/n)` are preserved, including strips with fewer than six physical pixels. The new editor materializes the occupied stages when saving an edited grouped map. An old loop maps to an enabled boot and ambient animation of the same pattern and period. An old one-shot maps to boot only. The first verified save writes schema 5.

An extension upload validates the whole candidate and commits and rereads the NVS record before publishing it as active. NVS commit success followed by readback failure leaves the controller RAM configuration unchanged and reports an unverified save. Reconnect and inspect the persisted state before retrying. On boot a corrupt schema-5 record can fall back to an intact historical record. An old firmware image ignores schema 5 and sees its previous schema-4 or older settings. Owner credentials, calibration binding, CAN capture files and diagnostic storage are outside the migration. No storage erase is used.

## ESP32 RMT and power

Arduino-ESP32 3.3.12 uses the ESP-IDF RMT copy encoder, four 64-symbol blocks, no DMA and an interrupt-refilled stream for long frames. The HAL borrows the encoded symbols until TX completion. The controller polls completion without waiting in the LED driver and preserves in-flight symbols through a timeout. A rejected enqueue can recover from idle. A stuck transfer after 100 ms quarantines the driver and its buffer until reset. The confirmed frame is updated only on TX completion. CAN ingestion, watchdog servicing and BLE telemetry can continue while the transfer is pending. Main-task flash operations and GATT config dispatch wait for the transfer to finish or be quarantined to reduce refill starvation. An RMT completion confirms peripheral completion only. [Arduino 3.3.12 HAL source](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/esp32-hal-rmt.c), [Arduino RMT API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/rmt.html), [ESP-IDF RMT guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html)

The verified encoding is GRB, most-significant-bit first. At 10 MHz the high/low pairs are 0.3/0.9 µs for zero and 0.8/0.6 µs for one. Every frame includes 300 µs low reset time. A 64-pixel frame uses 1,537 payload symbols and has a nominal wire duration of 2.143-2.450 ms before software overhead. The pitch between half-buffer refills is about 154-179 µs while sending data. Classic ESP32 refill latency with BLE is a physical validation gate. [Espressif RMT FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/rmt.html), [Worldsemi WS2812B-V5 datasheet](https://files.keeb.supply/products/ws2812b-rgb-led/datasheet.pdf)

GPIO27 is data only. At 5 V use a suitable 3.3 V to 5 V logic buffer, common ground, and the existing electrical protections. Full-white supply allowance at 60 mA per pixel is 0.36 A for six and 3.84 A for 64. These are design budgets, with the actual draw dependent on strip revision, brightness and color. Check voltage drop and provision the strip supply and injection points for its actual wiring. [Adafruit NeoPixel powering guide](https://learn.adafruit.com/adafruit-neopixel-uberguide/powering-neopixels)

## Hardware acceptance checklist

- Record controller/phone versions and pre-update NVS, calibration and capture status. Use the existing signed OTA procedure and retain previous backups.
- Exercise six, 12 and 64 physical pixels on a bench, plus the intermediate lengths where available. Capture GPIO27 and first-pixel DIN under BLE traffic, log recording and configuration writes. Check data pulse widths, reset low, final pixel, refresh behavior, output stalls and voltage drop at full white.
- On a safely isolated controller verify Normal and Reverse, a shorter active segment, warning override, and a 12-entry individual mapping. Check 12 independent activations using synthetic RPM, then repeat for automatic and the 3/3/2/2/1/1 group counts.
- Confirm the running count stays stable until reboot. Save and power-cycle, then compare the full snapshot and verify the driver uses the saved count. Check boot and ambient settings independently.
- Leave each ambient pattern running with BLE disconnected for over ten minutes. Exercise fresh zero, live RPM, loss during running, stale zero, unrelated CAN traffic after quiet startup, true CAN fault, and power-off. Check commissioning and preview priority with an authorized owner session.
- Install the APK over the existing debug app and check owner access, preset restoration, BLE reconnection and service lifecycle. Interrupt a configuration upload, reconnect and inspect the full snapshot. Check simulated RPM remains separate from telemetry and that the installed controller remains receive-only.
