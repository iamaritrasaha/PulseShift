# Software verification

PulseShift includes automated regression tests covering decoders, profiles, stale/fault blanking, power-loss safe LittleFS capture, candidate monitor bounds, and release guards.

~~~sh
bash test/run.sh
~~~

Native C++ checks enable ASan, UBSan, and warnings treated as errors.

For firmware compilation, use Arduino IDE or Arduino CLI with ESP32 board support (esp32:esp32@3.3.12) as documented in [the README](../README.md).

## Verification scope and hardware limits

- DEMO uses full synthetic CAN frames through the shared decoder and dashboard. It does not validate TWAI or a physical CAN bus.
- [TWAI reproduction and evidence](../docs/CAN-INVESTIGATION.md) document early Wokwi simulator investigations.
- No host compilation or simulation result qualifies physical LED power or vehicle behavior. Physical motorcycle acquisition was achieved on the 2025 Bajaj Pulsar NS125 ABS at 500 kbps (Session 2: 22,542 frames, 18 IDs, 0 corrupt records), but RPM numerical calibration remains strictly UNVERIFIED.
- [the README](../README.md) documents verified wiring, live serial commands, and LittleFS export; all candidate RPM results remain UNVERIFIED until independent calibration is complete.
