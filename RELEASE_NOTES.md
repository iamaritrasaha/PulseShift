# PulseShift Firmware Release Notes

## Release 2.2.0 - Production Cleanup

- **Single production operating path**: passive listen-only CAN monitoring, unified RPM decoder, six-LED rendering and BLE run always; flash capture is a runtime-toggleable subsystem. The compile-time mode matrix (CAN_CAPTURE / VEHICLE_RPM / DEMO / TWAI_TEST), the legacy ShiftLightEngine, the profile-based RpmState pipeline and the TWAI self-test transmit path were removed entirely.
- **OLED support removed**: SSD1306/Adafruit GFX dependencies, wiring configuration, rendering code, build variants and test stubs are gone. The LED controller (`led_controller.*`) owns the six WS2812B pixels with no display dependency; the Android phone is the display.
- **Complete BLE command interface (protocol 2.2.0)**: every device command is available over BLE - full LED configuration, decoder parameters (width/endianness/offset/clamp), threshold hysteresis, firmware-driven calibration workflow, diagnostics TLV blob, bounded CAN capture page export, capture recording toggle, configuration reload, diagnostics-only clear and ESP32 restart with explicit confirmation. All writes require a session bound to the authenticating connection.
- **Reliability**: bounded SPSC command queues, loop-task watchdog subscription, NVS config loaded before the first LED frame, restore-defaults preserves owner credentials (recovery is the distinct BOOT-window re-enrollment), RPM staleness blanks the LEDs instead of freezing the last value.
- **Verified Physical Hardware Acquisition**: Real motorcycle CAN bus acquisition achieved on a 2025 Bajaj Pulsar NS125 ABS at 500,000 bps (Session 2: 22,542 records across 3 stages, 18 CAN IDs, 0 corrupt records, ~82.5s duration).
- **Provisional Engine Speed Candidate**: Candidate engine-speed frames identified on arbitration ID `0x301`, payload byte 0 (moving from raw 0 at engine-off, to ~35-40 at idle, to 240 during sweeps; provisional scale: 40.0).
- **Explicit Calibration Status**: Numerical RPM calibration remains strictly PROVISIONAL and UNVERIFIED against an independent physical tachometer. Physical shift lights remain disabled until the firmware-driven calibration workflow binds a verified record on the vehicle.
- **Hardware Wiring (unchanged)**: Breakout CTX pin is connected directly to ESP32 3V3 (no series or pull-up resistor). ESP32 GPIO17 is physically disconnected. Breakout CRX connects to ESP32 GPIO16.
- **Toolchain**: Tested and verified with Arduino-ESP32 core 3.3.12 in standard Arduino IDE.
