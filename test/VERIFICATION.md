# Software verification

Run from the repository root inside Ubuntu WSL.

~~~sh
bash test/run.sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle
python3 scripts/verify_firmware_isolation.py
python3 -O scripts/verify_firmware_isolation.py
~~~

The host suite covers decoding, profiles, stale/fault blanking, recovery, GPIO
validation, bounded logging, capture analysis and release guards. Native C++
checks enable ASan, UBSan and warnings as errors.

For an Arduino distribution, run package_arduino.py, verify_arduino_package.py
and verify_arduino_matrix.py under scripts/. Use the matching version, ESP32
core 2.0.17 and the pinned libraries in [the Arduino guide](../arduino/README.txt).
The matrix compiles 15 configurations and checks linked mode isolation; its
work directory must not already exist.

## Simulator limits

- DEMO uses full synthetic CAN frames through the shared decoder and dashboard.
  It does not validate TWAI or a physical CAN bus.
- The isolated TWAI test still has a known transmission-completion failure.
  Keep its checks enabled and report the failure honestly.
- [TWAI reproduction and evidence](TWAI_WOKWI.md) remain available.

No compilation or simulation result qualifies the actual LED strip, CAN module
or NS125 protocol. Keep detailed run logs and reports local.
