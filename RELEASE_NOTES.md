# PulseShift v0.3.0 | Arduino IDE software prerelease

PulseShift is the new name of REVIX LINK. This is a proposed software prerelease,
not a stable or plug-and-play motorcycle product.

## Changes
- Rename firmware, diagrams, repository links and development paths to PulseShift.
- Generate PulseShift/PulseShift.ino in PulseShift-Arduino-v0.3.0.zip.
- Use PULSESHIFT_ settings; retain checked REVIX_ configuration compatibility.
- Accept PulseShift and historical REVIX LINK startup markers in capture logs.

DEMO remains the default six-LED simulation. CAN_CAPTURE remains receive-only.
VEHICLE_RPM stays unconfigured for the unverified 2025 Indian NS125 ABS.
Animations, GPIO27/16/17, brightness 40/255, Classic style and OLED-off defaults
are unchanged. Historical v0.1.0, v0.2.0 and v0.2.1 assets are not renamed.

## Install
Verify the matching ZIP/checksum, extract the PulseShift folder and open
PulseShift.ino. Use ESP32 core 2.0.17 and the pinned libraries in README.txt.
Keep CAN disconnected for the demo and follow the wiring guide.

## Limits
Motorcycle RPM integration has not been physically qualified.
The actual SN65HVD230 transceiver's electrical passivity has not been physically qualified.
Direct 3.3V LED signal compatibility with the actual 5V strip has not been physically qualified.
The motorcycle connector pinout, CAN bitrate, RPM encoding and calibration remain unverified.
Wokwi TWAI transmission completion remains unverified.
The existing self-test still receives a frame with TX pending and halts once;
this is not proof of physical CAN success.

A future v0.3.0 publication requires exact-commit tests, package verification and
owner approval. No release or tag is created by the name migration.
