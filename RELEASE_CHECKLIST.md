# Release checklist

- Confirm the changes are approved and CI passes for the exact commit.
- Decide whether to grant redistribution and modification rights. `COPYRIGHT` currently reserves all rights, and there is no explicit `LICENSE` file. Do not describe the project as open-source unless an approved license supports that statement.
- Recheck the Arduino ZIP contents, SHA-256 file, and tested Arduino-ESP32 2.0.17 build.
- Confirm that demo mode remains the default and that no NS125 CAN bitrate or RPM mapping is claimed as verified.
- Push a semantic-version tag such as `v1.0.0` only when the release is approved. The tag workflow publishes the verified ZIP and checksum.
