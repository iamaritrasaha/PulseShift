# CAN integration verification

Software candidate: v0.2.0 preview. Base: 5017e6725347171b015eca735552fe60ddfbb341.
All local commands, builds, tests, browser automation and artifacts run in Ubuntu
WSL under /home/hrik/projects/REVIX_LINK_ESP32. No vehicle/USB connection or
firmware upload to physical hardware is performed.

## Reproduce the local checks

~~~sh
bash test/run.sh
~/.platformio/penv/bin/pio run -j 4 -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle
python3 scripts/verify_firmware_isolation.py
python3 scripts/package_arduino.py --version v0.2.0 --output-dir .pio/can-integration/release-candidate
python3 scripts/verify_arduino_package.py --version v0.2.0 --zip .pio/can-integration/release-candidate/REVIX-LINK-Arduino-v0.2.0.zip
python3 scripts/verify_arduino_matrix.py --zip .pio/can-integration/release-candidate/REVIX-LINK-Arduino-v0.2.0.zip --version v0.2.0 --work-dir .pio/can-integration/release-matrix --arduino-cli /home/hrik/.local/bin/arduino-cli
python3 tools/can_investigate.py test/fixtures/synthetic_capture.log --reference test/fixtures/synthetic_rpm.csv --max-skew-ms 30 --output .pio/can-integration/analysis-example
python3 assets/readme/generate.py
~~~

The matrix work directory must be absent before a new run. Local Arduino CLI
is 1.5.1, core 2.0.17, with the pinned libraries in platformio.ini. CI uses
Arduino CLI 1.1.1 and the same core/libraries, with its explicit ctags setup.

The original 33-case / 638788-assertion suite passed before implementation.
It remains intact. The expanded sanitizer suite adds 7186 profile/path checks,
162 driver checks per receive-only mode, both existing UART/OLED checks and
12 Python tests. It covers qualified synthetic profiles, bit extraction,
validity/mux/checksum/counters, lost/stale signals, zero RPM, full timer-lap
expiration, recovery, LED outputs, driver API choices and GPIO conflicts.
Both capture and vehicle driver doubles omit a transmit implementation,
so any application call would fail linkage. A compile-only replay-object check
protects mode 3/4 even before linker garbage collection. Every extracted Arduino
variant also checks its linked ELF for forbidden transport/generator symbols.

## Simulator execution

The final demo ELF ran in Wokwi's documented custom-firmware template in Linux
Chromium. Only the browser's normal editor, file chooser and serial input were
used. The repository circuit was pasted into an unsaved session. No project
was published or saved on Wokwi. The browser UART adapter reads actual displayed
UART text and sends normal serial commands; it never changes simulator RPM,
controller state, received frames, or TX events.

~~~sh
~/.platformio/penv/bin/python test/wokwi_runtime.py --browser-cli .pio/can-integration/browser/node_modules/.bin/playwright-cli --browser-session revix --timeout-scale 2 --output-dir .pio/can-integration/wokwi-final
~~~

This passed the original 23 replay/runtime checks plus a custom-firmware banner
guard. Existing RFC2217 operation is preserved: omit the browser flags and use
the Remote WSL Wokwi extension with its optional documented serial bridge.
The browser CLI is optional development tooling, not an Arduino setup step.

A first browser-harness attempt timed out because the serial input has no
explicit type attribute. The selector was corrected and tested. A subsequent
browser restart loaded the template sketch instead of the custom ELF; the
test correctly failed. Re-uploading the exact ELF through the documented
loader passed. The banner guard and unchanged-ELF check prevent silently
crediting the wrong firmware. Failed-run logs remain in the local build area.

Rendered OLED/progressive LEDs were observed at simulated 4200 RPM. SHIFT!
and the flash-off phase were captured at 6000 RPM; stale data showed -- RPM
with all LEDs off. Native pixel assertions verify both flash phases. A slow
headless browser can sample a 10Hz blink at the same phase; a still image is
not proof of its full temporal behavior.

The current isolated TWAI ELF reproduced one self-received frame, pendingTX=1,
completed=0 and alert mask 0x4. It emitted exactly one completion timeout and
one successful stop, then stayed quiet. This does NOT verify TWAI transmission.
The current VEHICLE_RPM ELF booted with profile status UNVERIFIED, ready=0,
CAN disabled, configured=0/healthy=0, no received frames and blank LEDs.

## Example investigation output

The committed fixture is artificial and includes a format collision (same ID
with extended frames), unrelated traffic, a remote frame and a truncated log
record. The investigator reports 253 accepted log records, four message groups,
one rejected record, and the planted standard ID 0x612, byte 2, 16-bit LE field
with scale 0.25 and offset -100 RPM. There are 84 aligned independent reference
samples, including a 28-sample chronological holdout. Its zero error is a
fixture property, not motorcycle evidence. Every candidate/vector is UNVERIFIED.
Exports are bounded representative samples; clocks, counter/checksum/mux rules
and independent sessions still require review.

## Release and hardware gates

GitHub Actions repeats native tests, four target builds, linked-symbol checks,
the synthetic analysis, deterministic packaging, 15 extracted Arduino variants
and invalid-configuration rejection. Review the workflow run for the exact
feature-branch commit. It does not connect hardware or publish a release.

Direct LED wiring is GPIO27 through 330 ohms to DIN, with separate 5V LED power
and mandatory common ground. It is the owner's chosen standard, not universal
3.3V/5V compatibility. The actual strip must be tested; an optional proper
74AHCT125 may be needed. Wokwi's ideal data/power models do not qualify it.

The NS125 connector, bitrate, passive signal encoding, timing, shift calibration
and actual SN65HVD230 passivity remain pending. The installed SDK explicitly
has CONFIG_TWAI_ERRATA_FIX_LISTEN_ONLY_DOM disabled; TX pad disconnection is
preserved, and hardware RS-high/TXD bias remains required throughout routing,
reset and power transitions. A six-pin module is not an automotive-protected
interface. Hardware installation remains blocked pending physical qualification.
No release/tag is created; the existing v0.1.0 remains intact.

## PR #3 review follow-up

The pre-review f7f65a6 native suite passed before editing. A new OLED regression
then failed against its demo-denominator implementation. The corrected OLED
reads the existing ShiftLightEngine calibration, so it shares verified vehicle
display limits and preserves the demo denominator. Native-only linker wrapping
supplies a synthetic VERIFIED profile to the real configuration factory; no test
profile selector or bypass is added to ESP32 firmware.

The new regression adds 202 assertions across DEMO and VEHICLE_RPM with UART
enabled and disabled. Its vehicle light maximum is 10000, decoder validity limit
12000 and demo maximum 6500. It checks zero, partial fill, exact maximum, clamping,
stale blanking, hardware fault, lost configuration, invalid DLC, valid recovery
and an unverified-profile boot. At 5000 RPM, the calibrated vehicle bar is 63 of
126 pixels; the demo bar retains its existing scale. Both modes exercise the
actual Dashboard draw calls.

The full local review commands, from the repository root in Ubuntu WSL, were:

~~~sh
bash test/run.sh
~/.platformio/penv/bin/pio run -j 4 -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle
python3 scripts/verify_firmware_isolation.py
python3 scripts/package_arduino.py --version v0.2.0 --output-dir .pio/pr3-review/package
python3 scripts/verify_arduino_package.py --version v0.2.0 --zip .pio/pr3-review/package/REVIX-LINK-Arduino-v0.2.0.zip
python3 scripts/verify_arduino_matrix.py --zip .pio/pr3-review/package/REVIX-LINK-Arduino-v0.2.0.zip --version v0.2.0 --work-dir .pio/pr3-review/arduino-matrix --arduino-cli /home/hrik/.local/bin/arduino-cli
python3 test/build_wokwi_twai_probes.py --arduino-cli /home/hrik/.local/bin/arduino-cli --output-dir .pio/pr3-review/probes
~/.platformio/penv/bin/python test/wokwi_runtime.py --browser-cli .pio/can-integration/browser/node_modules/.bin/playwright-cli --browser-session revix-review --timeout-scale 2 --output-dir .pio/pr3-review/wokwi-demo
python3 tools/can_investigate.py test/fixtures/synthetic_capture.log --reference test/fixtures/synthetic_rpm.csv --max-skew-ms 30 --output .pio/pr3-review/analysis
python3 assets/readme/generate.py
~~~

Results: all original and new native checks passed with warnings as errors,
ASan and UBSan; four PlatformIO builds and symbol-isolation checks passed;
all 15 extracted Arduino configurations and their linked-ELF checks passed.
The 25-file source ZIP contains the corrected dashboard and unchanged beginner
instructions. The extracted invalid-LED-count build is rejected with the intended
configuration error. Repeated ZIP generation is byte-identical.

The rebuilt demo ELF passed all 24 Wokwi runtime checks. The rebuilt vehicle ELF
booted UNVERIFIED with ready=0, CAN disabled, no accepted frames and all six LEDs
blank. The unchanged self-test target still FAILS TX completion, stops once and
stays quiet. Four minimal probes plus a disconnected-loopback negative control
reproduce the incomplete completion behavior. These are real simulator failures,
not physical CAN validation. See [the investigation](TWAI_WOKWI.md) and its linked
recorded UART evidence for source references, exact settings and limits.

The beginner README, direct GPIO27 wiring diagrams, UserConfig defaults,
COPYRIGHT, original simulator circuit and release tags are unchanged by this
follow-up. No vehicle connection or physical firmware upload was performed.
