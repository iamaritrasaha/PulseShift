# REVIX LINK v0.2.0 preview candidate

Prepared locally for owner review. No tag or public release is created by this work.

This preview improves the default full-frame Wokwi demonstration and isolated receive-only bench capture. It preserves visual styles, custom colors, direction, brightness, startup/smoothing, shift alerts with hysteresis, runtime controls and the optional OLED.

- Bench builds now honor the bitrate in UserConfig.h.
- Bus-error warning output is aggregated while retaining observation and driver counters.
- Malformed numbers, overlong commands and binary command input are rejected.
- Stopping replay stops manual frames too; valid zero and above-range rejection remain distinguishable.
- OLED traffic is capped at 10 Hz and unchanged screens are skipped.
- Startup timing begins after peripheral setup and the OLED identifies STARTUP.
- Supported pixel count is consistently 1..32; invalid classic-ESP32 GPIO selections are rejected.
- Custom palettes support intentionally dark pixels.
- Native, I/O and extracted-Arduino-package configuration coverage is expanded.

Default: synthetic full-frame replay, six WS2812B pixels on GPIO27, brightness 40/255, Classic/normal, red shift flash, 150 RPM hysteresis, demonstration start/shift/max 1500/5800/6500, serial 115200. Wokwi enables OLED; the downloadable Arduino sketch leaves it disabled. Receive-only capture defaults to bitrate 0 and has no enabled motorcycle RPM mapping.

The 2025 Indian NS125 ABS connector, bitrate, RPM encoding and completed harness remain unverified. This preview is not a validated plug-and-play motorcycle release. Experimental TWAI remains separate from replay; an echoed frame is not transmission completion. No firmware was flashed or connected to a motorcycle.

The source ZIP and matching SHA-256 are prepared through the existing deterministic packager. Installation and hardware instructions are in README.md and the packaged README.txt. Copyright (c) 2026 Aritra Saha. All rights reserved. Dependency notices/licenses remain separate.
