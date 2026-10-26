# Release checklist

- Confirm that all local tests pass inside Ubuntu WSL (`bash test/run.sh`).
- Confirm that all PlatformIO environments build cleanly (`esp32dev_wokwi`, `esp32dev_twai_test`, `esp32dev_bike`).
- Confirm that GitHub Actions CI passes for the exact commit across all configuration variants.
- Confirm that the generated Arduino source ZIP passes verification (`scripts/verify_arduino_package.py`) and matches its SHA-256 checksum.
- Confirm that `UserConfig.h` compile-time validations correctly reject out-of-range settings.
- Confirm that demo mode remains the default and that no NS125 CAN bitrate or RPM mapping is claimed as verified.
- Confirm that no `docs` directory has been reintroduced and no generated artifacts are tracked in Git.
- Confirm that no em dashes or AI references are present in documentation, release notes, or commit messages.
- Decide whether to grant redistribution and modification rights. `COPYRIGHT` currently reserves all rights, and there is no explicit `LICENSE` file. Do not describe the project as open-source unless an approved license supports that statement.
- Push a semantic-version tag such as `v1.0.0` only when the release is explicitly approved by the owner. The tag workflow publishes the verified ZIP and checksum.
