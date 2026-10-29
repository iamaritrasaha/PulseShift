# PulseShift v0.3.0 | Arduino IDE software prerelease

First PulseShift-branded Arduino release.

- Includes configurable six-LED RPM simulation and shift alerts.
- Adds improved passive CAN capture, independent-session offline RPM discovery and stricter profile evidence gates.
- DEMO remains the default; CAN_CAPTURE is receive-only; VEHICLE_RPM remains disabled until verified.
- Arduino package: PulseShift-Arduino-v0.3.0.zip, containing PulseShift/PulseShift.ino. Use ESP32 core 2.0.17.

Motorcycle RPM integration has not been physically qualified.
The actual SN65HVD230 transceiver's electrical passivity has not been physically qualified.
Direct 3.3V LED signal compatibility with the actual 5V strip has not been physically qualified.
The NS125 connector, CAN bitrate, passive broadcasts, RPM mapping and calibration remain unverified.
Wokwi TWAI transmission completion remains unverified.

This is a software prerelease, not a physically qualified motorcycle accessory.
