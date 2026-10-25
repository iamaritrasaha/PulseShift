# Verification — 2026-10-25

## Hardware target update: 2025 NS125 ABS

Updated `include/config.h`, `src/main.cpp`, `src/dashboard.cpp`, `include/simulated_ecu.h`, README and HARDWARE.md for the user's ESP32 / SN65HVD230 / WS2812B hardware and 2025 Pulsar NS125 ABS. Retained the original six pixels and SSD1306. Centralized LED brightness, added the bike/transceiver identity to the hardware startup log, and made bike mode default to bitrate 0 even if the PlatformIO bitrate definition is omitted. The vehicle RPM mapping remains disabled. Replay remains selected for Wokwi.

TI's datasheet confirms the SN65HVD230 3.3 V supply and RS-high receive-only operation; HARDWARE.md now gives the chip-to-ESP32 wiring reference and module-specific checks. The official Bajaj manual catalog was checked, but the linked current ABS PDF fetch timed out. No 2025 connector, bitrate or RPM mapping was verified or inferred.

Executed successfully in WSL:

```sh
/home/hrik/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_bike
bash test/run.sh
```

Both builds passed; 14 cases / 131683 assertions passed with sanitizers. A separate `g++ -std=c++17 -x c++ -fsyntax-only -DREVIX_MODE=3 -I/home/hrik/projects/REVIX_LINK_ESP32/include -` compilation of stdin static assertions verified disabled bitrate without the PlatformIO flag and the RX16/TX17/LED27 pin settings. `xtensa-esp32-elf-nm -C` confirmed no `twai_transmit`, `CanTransport::sendTest`, `demonstrationFrame` or `ReplayTransport` symbols in the bike binary.

Logs are `.pio/reports/ns125-hardware-build.log`, `ns125-hardware-tests.log` and `ns125-bike-symbols.txt`. No new simulator or hardware execution was performed. The electrical design, module RS wiring, bike CAN availability and real RPM decoding still require verification.

## Update after the user's Wokwi run

The user supplied a runtime log reproducing one self-received frame, zero TX completion, pending TX=1 and RX_DATA-only alerts (0x4). Timeout occurred once, then the controller stopped successfully. Full interpretation and an excerpt are in CAN-INVESTIGATION.md. This is a failed TWAI completion test, not a passing CAN transport test.

Application serial printf/frame lines now use CRLF to address the staircase output. After a halt, the application prints one final stopped-state report then stops periodic status logging. The dashboard continues refreshing. `include/config.h` now explicitly selects `REVIX_REPLAY`; README and testing instructions match. This is a manual build selection, never a hidden runtime fallback.

Executed with TWAI selected after the logging edits:

```sh
/home/hrik/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_bike
bash test/run.sh
```

Both builds passed. All 14 cases / 131683 assertions passed with sanitizers. Then selected replay and ran:

```sh
/home/hrik/.platformio/penv/bin/pio run -e esp32dev_wokwi
```

Passed. The **current firmware on disk is FRAME_REPLAY_TEST**. Logs: `.pio/reports/serial-fix-build.log`, `serial-fix-tests.log`, `replay-after-stall-build.log`. No Wokwi runtime was executed by the agent; serial presentation fixes and replay behavior await the user's next run. The earlier verification below records the preceding state, when TWAI was selected.

## Simplified workflow

The active project is `/home/hrik/projects/REVIX_LINK_ESP32` in WSL Ubuntu. `src/main.cpp` is the Arduino entry point. Ctrl+Shift+B directly runs PlatformIO; F1 -> Wokwi: Start Simulator launches the built firmware.

Changed `platformio.ini`, `include/config.h`, `.vscode/tasks.json`, root `wokwi.toml`, root `diagram.json`, README and testing/hardware documentation. Added `test/run.sh` for optional native C++ tests. The root circuit now always contains the ESP32, OLED, six LEDs and simulation loopback. There are two firmware environments: Wokwi and bike.

Custom Python preparation/check scripts, the redundant circuit template, obsolete configuration tests, optional SocketCAN tooling and the minimal CAN-only probe were archived at `/home/hrik/project-archives/REVIX_LINK_ESP32/2026-10-25/workflow-before-simplification`. Their original configuration and documentation are preserved there. No active build depends on that archive.

## Commands executed and actual results

From the project directory in WSL:

```sh
/home/hrik/.platformio/penv/bin/pio run -j 4 -e esp32dev_wokwi -e esp32dev_bike
bash test/run.sh
```

Both firmware environments passed. Host tests passed: 14 cases, 131683 assertions, with ASan/UBSan and warnings as errors.

Temporarily changed the single `REVIX_MODE` setting to `REVIX_REPLAY`, then ran:

```sh
/home/hrik/.platformio/penv/bin/pio run -j 4 -e esp32dev_wokwi
```

Replay compiled successfully. Restored `REVIX_TWAI_TEST`, then ran the exact default Build task command and host tests:

```sh
/home/hrik/.platformio/penv/bin/pio run -e esp32dev_wokwi
bash test/run.sh
```

Both passed. The final firmware on disk is TWAI self-test with the dashboard. Logs are in `.pio/reports/simple-build.log`, `simple-replay-build.log`, `simple-final-build.log` and `simple-host-tests.log`.

Also inspected the two binaries using:

```sh
/home/hrik/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm -C .pio/build/esp32dev_bike/firmware.elf
/home/hrik/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm -C .pio/build/esp32dev_wokwi/firmware.elf
```

The second inspection was performed while replay was selected. Bike had no `twai_transmit`, `CanTransport::sendTest`, `demonstrationFrame` or `ReplayTransport` symbols. Replay had no `twai_` or `CanTransport::` symbols. These checks establish binary separation, not physical electrical safety.

## Remaining limits

No simulator run was performed during this simplification, per the user's request to run Wokwi manually. A successful build is not proof of continuous TWAI TX/RX in Wokwi. Replay tests complete-frame parsing and dashboard behavior only.

The prior missing `wokwi.toml` was found under `legacy/`; it is now fixed at the project root with relative paths matching the default build. The original repeated timeout was an application latch bug. The unresolved pending-TX symptom requires actual simulator output; the self-received frame alone does not prove TX completion. See `CAN-INVESTIGATION.md` for evidence and references.

Bench testing still needs a suitable transceiver, verified voltage/grounding/termination, a known CAN source and physical passivity measurements. Motorcycle integration additionally requires verified network access, bitrate, RPM mapping and automotive power protection. No hardware was flashed or connected.
