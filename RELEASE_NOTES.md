# REVIX LINK v0.2.0 preview release candidate

This candidate adds passive CAN capture, a gated vehicle RPM path and offline
investigation tools while keeping the default six-LED demo and beginner guide.
The existing v0.1.0 release and tag remain unchanged. No release tag is created.

- DEMO remains the default, including serial controls, all LED styles and the
  20-second full-frame replay, malformed data, silence and recovery.
- CAN_CAPTURE uses listen-only TWAI, GPIO16 RX, disconnected GPIO17 TX, bounded
  machine-readable raw/status logs and explicit driver/log loss counters.
- VEHICLE_RPM uses a qualified profile and the same RPM state and LED engine.
  The NS125 profile is UNVERIFIED and empty; the controller stays disabled.
- Profiles define bounded unsigned bit extraction, rational scale/offset,
  freshness, optional validity/multiplex/counter/checksum rules and an explicit
  custom-validator extension. Protocol and shift calibration evidence gates
  must pass before a profile is usable.
- Offline analysis compares raw logs with timestamped independent RPM, ranks
  unsigned candidate fields and exports review vectors. It never enables a
  firmware profile. Included capture/reference data is explicitly synthetic.
- New tests cover protocol validation, stale/invalid blanking, resumed traffic,
  timestamp rollover, driver routing/faults/overflow and UART backpressure.

Default hardware: original ESP32, six WS2812B GRB 800kHz pixels at GPIO27,
direct GPIO27 data through 330 ohms, brightness 40/255, Classic/normal and OLED
disabled in Arduino. Direct 3.3V data compatibility must be tested on the actual
5V strip; a proper 74AHCT125 is an optional remedy, not standard hardware.
The existing Wokwi environment retains its simulated OLED. CAN capture bitrate
defaults to zero. Demo thresholds are separate from vehicle calibration.

Software builds/tests are distinct from electrical or motorcycle validation.
The exact 2025 Indian NS125 ABS connector, passive RPM protocol, shift
calibration and actual module passivity remain pending. Hardware installation
is not approved by this candidate. No motorcycle connection is performed.

The reproducible Arduino ZIP is prepared and compiled from an extracted copy.
See arduino/README.txt for setup, capture format, analysis and physical gates.
Copyright (c) 2026 Aritra Saha. All rights reserved.
