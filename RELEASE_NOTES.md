# PulseShift Firmware Release Notes

## Status Overview

- **Verified Physical Hardware Acquisition**: Real motorcycle CAN bus acquisition achieved on a 2025 Bajaj Pulsar NS125 ABS at 500,000 bps (Session 2: 22,542 records across 3 stages, 18 CAN IDs, 0 corrupt records, ~82.5s duration).
- **Provisional Engine Speed Candidate**: Candidate engine-speed frames identified on arbitration ID `0x301`, payload byte 0 (moving from raw 0 at engine-off, to ~35-40 at idle, to 240 during sweeps; provisional scale: 40.0).
- **Explicit Calibration Status**: Numerical RPM calibration remains strictly PROVISIONAL and UNVERIFIED against an independent physical tachometer. Physical shift lights remain disabled in vehicle mode (`RPM_MAPPED=NO`).
- **Bench Shift-Light Testing**: 6-LED WS2812B shift-light progression and alert testing is available safely on the bench via `RPM TEST <val>` without running the motorcycle.
- **Permanent CAN Diagnostics**: Passive CAN capture, LittleFS ring-buffered storage, CRC16 validation, power-loss safe recovery, live 10 Hz candidate streaming, and paginated `DUMP` export are permanent firmware features.
- **Hardware Wiring**: Breakout CTX pin is connected directly to ESP32 3V3 (no series or pull-up resistor). ESP32 GPIO17 is physically disconnected. Breakout CRX connects to ESP32 GPIO16.
- **Toolchain**: Tested and verified with Arduino-ESP32 core 3.3.12 in standard Arduino IDE.
