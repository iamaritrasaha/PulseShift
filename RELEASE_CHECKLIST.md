# Release checklist

- Confirm that all local tests pass inside Ubuntu WSL (`bash test/run.sh`).
- Confirm that all PlatformIO environments build cleanly (`esp32dev_wokwi`, `esp32dev_twai_test`, `esp32dev_bike`, `esp32dev_vehicle`).
- Confirm that GitHub Actions CI passes for the exact commit across all configuration variants.
- Confirm that the generated Arduino source ZIP passes verification (`scripts/verify_arduino_package.py`) and matches its SHA-256 checksum.
- Confirm that `UserConfig.h` compile-time validations correctly reject out-of-range settings.
- Confirm that demo mode remains the default and that no NS125 CAN bitrate or RPM mapping is claimed as verified.
- Confirm that local `docs` remains intact while no `docs` paths or generated artifacts are tracked or packaged.
- Confirm public attribution matches COPYRIGHT and all dependency notices are preserved.
- Decide whether to grant redistribution and modification rights. `COPYRIGHT` currently reserves all rights, and there is no explicit `LICENSE` file. Do not describe the project as open-source unless an approved license supports that statement.
- Push a semantic-version tag such as `v0.2.0` only when the release is explicitly approved by the owner. The tag workflow publishes the verified ZIP and checksum.

- Confirm that the final firmware hash matches the recorded Wokwi runtime test, and actual pixels/OLED were observed.
- Confirm the temporary RFC2217 test port is removed from the normal configuration.
- Keep stable publication gated until target-variant wiring, RPM mapping and physical passivity are verified.

- Verify the NS125 profile remains UNVERIFIED with no bitrate or signal mapping.
- Run offline analysis and vehicle/driver regression tests with synthetic fixtures.
- Verify both receive-only ELF images exclude transmit and synthetic ECU symbols.
- Verify direct-drive LED compatibility on the actual strip before hardware claims.
