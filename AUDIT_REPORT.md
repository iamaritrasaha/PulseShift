# Safety and reliability audit

Audit baseline: f16f5c61594ddcb21b89ecc62a355eec1b4b6862.
Scope: tracked firmware, configuration, tests, investigation tools, packaging,
automation, instructions and diagrams. This report records reproduced defects;
it does not certify the firmware or hardware as defect-free.

## Confirmed findings before implementation

Line references below identify the baseline revision.

### A1: Delayed safety blanking (Medium)

- Location: src/shift_light.cpp:212.
- Root cause: the animation throttle runs before non-live status handling.
- Reproduction: render 6000 RPM at 100 ms with a 250 ms interval, then signal
  Fault at 102 ms. All six pixels remain lit.
- Impact: invalid/stale/fault indications can retain the prior RPM for up to the
  configured refresh interval. Supported intervals extend to 250 ms.
- Correction: bypass visual throttling for non-live states and allow immediate
  recovery when valid data resumes.
- Regression: every non-live status inside the interval, startup and shift alert,
  timer rollover, repeated blanking and immediate recovery.
- Compatibility: live animation timing and public options remain unchanged.

### A2: Redundant LED refresh (Low)

- Location: src/shift_light.cpp:213.
- Root cause: a throttled call returns the previous render's dirty flag.
- Reproduction: after a changed render at 100 ms, the same input at 101 ms returns
  true despite no pixel change.
- Impact: unnecessary strip writes consume time available for frame processing.
- Correction: report false when a live animation update is throttled.
- Regression: unchanged throttled calls, later brightness/style changes and
  hardware-facing dashboard show-call counts.
- Compatibility: actual pixel changes continue to trigger output.

### A3: Capture sessions combined across reboot (Medium)

- Location: tools/can_investigate.py:40.
- Root cause: startup banners are ignored; a large timestamp decrease is treated
  as rollover even when a reboot banner separates the frames.
- Reproduction: t=4294967200, startup banner, t=10 yields two joined frames.
- Impact: incorrect timing can contaminate RPM-reference alignment.
- Correction: reject an explicit startup banner after received frames and require
  separate session files. Preserve legitimate rollover without a startup marker.
- Regression: initial startup banner, reboot at high/low uptime, genuine rollover.
- Compatibility: single-session capture format and output schema are unchanged.

### A4: Optimized Python bypasses verification (Medium)

- Locations: scripts/verify_firmware_isolation.py:19,
  scripts/verify_arduino_matrix.py:44, :55, :58.
- Root cause: required checks use assertions, removed by python -O or PYTHONOPTIMIZE.
- Reproduction: an isolated nm fixture containing forbidden bike twai_transmit
  fails normally but is reported passed under optimization.
- Impact: verification can falsely approve mode isolation or skip matrix integrity.
- Correction: explicit exceptions for forbidden/missing symbols, configuration
  replacement counts and linked ELF counts.
- Regression: normal/optimized execution with valid and forbidden symbols,
  missing self-test transmitter, missing/duplicate config and missing/extra ELF.
- Compatibility: successful ordinary builds retain their existing outputs.

### A5: UART diagnostics pin conflicts accepted (Medium)

- Locations: UserConfig.h:129, src/main.cpp:24.
- Root cause: GPIO output capability is checked, but Serial's UART0 GPIO1/GPIO3
  ownership is absent from conflict validation.
- Reproduction: LED GPIO1 and GPIO3 both compile while diagnostics are enabled.
- Impact: UART and LED/CAN/OLED routing can interfere on nonstandard configurations.
- Correction: reject diagnostic conflicts for LED, reserved CAN and enabled OLED
  assignments. Disabled OLED pins do not claim UART ownership.
- Regression: each UART pin and peripheral, diagnostics on/off, OLED on/off.
- Compatibility: defaults unchanged; formerly accepted conflicting configurations
  become compile errors. Disabling diagnostics is not electrical qualification of
  the USB-UART wiring.

### A6: Malformed capture records evade resource limit (Medium)

- Location: tools/can_investigate.py:48, :63.
- Root cause: the limit counts accepted frames only; rejected records accumulate
  independently. Input lines also have no length limit.
- Reproduction: five malformed RX records are retained with limit=2.
- Impact: an invalid log can consume memory beyond the selected record limit.
- Correction: count all RX attempts, including malformed records, and bound each
  log line before parsing.
- Regression: malformed-only and mixed logs at/above the boundary, oversized
  lines, ordinary fixture analysis.
- Compatibility: --max-frames becomes a stricter RX-record budget. Normal UART
  records remain far below the 4096-character line cap.

## Reviewed uncertainties and unchanged restrictions

No Critical or High defect has been confirmed. Partial excerpts alone cannot
prove global absence of defects. Existing sanitizer, decoder, driver and
configuration tests provide independent checks of uncertain review judgments.

- Software listen-only and detached GPIO17 do not establish electrical passivity
  of the actual SN65HVD230 module, including startup/reset, RS wiring and TXD bias.
- The 2025 NS125 ABS bitrate, connector pinout, RPM mapping and shift calibration
  remain unverified. Its profile remains UNVERIFIED and vehicle operation fails
  closed without qualifying evidence.
- Direct GPIO27 through 330 ohms to 5V WS2812B DIN remains the standard wiring;
  reliable recognition of 3.3V data requires checking the actual strip.
- Wokwi self-reception is not completed CAN transmission. Preserve the recorded
  pending-TX/completion timeout limitation and the failing diagnostic test.
- Candidate correlation requires independent sessions and reference RPM;
  counters/checksums can correlate spuriously. Suggestions never enable firmware.
- Dependency/action version refresh and further electrical qualification are
  maintenance/bench work, not demonstrated software fixes in this audit.

## Verification record

Baseline bash test/run.sh passed before implementation on 28 October 2026,
including native ASan/UBSan builds, Python tests, receive-only driver tests and
OLED calibration tests. The new regressions failed before the fixes and passed afterward.


### Fix verification, 28 October 2026

- All six findings corrected with focused changes. No CAN driver or vehicle
  mapping changes were needed.
- The new LED test failed independently in its blanking and refresh cases against
  the original implementation. The corrected Python regressions failed before
  implementation (23 subtest failures), then passed with the fixes.
- The final host suite passed: original 33 cases / 638788 assertions, new immediate
  blanking/refresh cases, 7186 vehicle checks, 18 CAN/tool Python tests, receive-only
  driver tests (162 checks per mode), 202 OLED assertions and 10 release tests.
  Native compilations use ASan, UBSan and warnings as errors.
- All four PlatformIO environments passed, with no compiler warnings in this run.
  Real linked images passed symbol isolation normally and under python -O.
- All 15 extracted Arduino configurations passed with ESP32 core 2.0.17 and the
  pinned libraries. Their linked mode-isolation checks also passed. The matrix
  compile logs contained no compiler warnings.
- GCC -fanalyzer compilation of decoder, vehicle profile, replay and LED engine
  passed with warnings as errors. Optimized-environment script regressions passed.
- The 25-file Arduino ZIP validated and reproduced byte-for-byte. Its local audit
  artifact SHA-256 is cd5ef7b5da991adafd8044dbf06eb5db5fbf02e23b9c9d827a110c39836d8733.
  This is an audit build, not a replacement of a published release asset.
- Five SVGs regenerated identically. Beginner README, diagrams, default settings,
  COPYRIGHT, published release notes, CAN restrictions and simulator files remain
  unchanged.
- The exact rebuilt demo ELF passed all 24 actual Wokwi replay/runtime checks.
  This exercises full synthetic frames and the shared processing path, not TWAI.
- The exact rebuilt TWAI self-test still FAILED transmission completion:
  one RX, pendingTX=1, completed=0, alerts=0x4, then exactly one timeout and stop.
  After the stopped-state report, the observer saw 54.1 wall seconds of quiet.
  This preserved known failure is not a successful CAN transport test.
- The rebuilt vehicle ELF booted with status=UNVERIFIED, ready=0, CAN disabled,
  configured=0 and healthy=0. No RX or synthetic fallback appeared in its UART.
- One orchestration timeout interrupted a repeated host run and runtime observer.
  Their partial evidence was retained; separate bounded jobs subsequently
  completed. No firmware/test condition was weakened.

### Commands executed

Run from /home/hrik/projects/REVIX_LINK_ESP32 inside Ubuntu WSL. The audit output
prefix below is abbreviated to avoid repeating a long path.

~~~sh
out=.pio/audit-2026-10-28/resume
bash test/run.sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle
python3 scripts/verify_firmware_isolation.py
python3 -O scripts/verify_firmware_isolation.py
PYTHONOPTIMIZE=1 python3 -m unittest discover -s test -p "test_can_isolation_checks.py"
python3 scripts/package_arduino.py --version v0.2.0 --output-dir "$out/assets"
python3 scripts/verify_arduino_package.py --zip "$out/assets/REVIX-LINK-Arduino-v0.2.0.zip" --version v0.2.0 --extract-to "$out/extracted"
python3 scripts/verify_arduino_matrix.py --zip "$out/assets/REVIX-LINK-Arduino-v0.2.0.zip" --version v0.2.0 --work-dir "$out/matrix" --ctags /home/hrik/.arduino15/packages/builtin/tools/ctags/5.8-arduino11 --arduino-cli /home/hrik/.local/bin/arduino-cli
python3 scripts/package_arduino.py --version v0.2.0 --output-dir "$out/assets-repeat"
cmp "$out/assets/REVIX-LINK-Arduino-v0.2.0.zip" "$out/assets-repeat/REVIX-LINK-Arduino-v0.2.0.zip"
python3 assets/readme/generate.py
python3 tools/can_investigate.py test/fixtures/synthetic_capture.log --reference test/fixtures/synthetic_rpm.csv --max-skew-ms 30 --output "$out/analysis"
~/.platformio/penv/bin/python test/wokwi_runtime.py --browser-cli .pio/can-integration/browser/node_modules/.bin/playwright-cli --browser-session revix-audit --timeout-scale 2 --output-dir "$out/wokwi-demo"
git diff --check
~~~

Static analyzer compilation ran in the audit output directory:

~~~sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -fanalyzer -I../../../include -c ../../../src/rpm_decoder.cpp ../../../src/vehicle_profile.cpp ../../../src/shift_light.cpp ../../../src/simulated_ecu.cpp
~~~

The isolated TWAI and vehicle ELFs were uploaded through the normal Wokwi editor
firmware chooser in an unsaved Linux Chromium session. Private observers read
the actual displayed UART only. The transmitting TWAI environment was never
flashed or connected to a motorcycle. See test/TWAI_WOKWI.md for the existing
minimal probe reproduction and limitations.
