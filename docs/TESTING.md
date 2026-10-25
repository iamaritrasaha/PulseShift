# Tests and CAN diagnostics

Run commands in `/home/hrik/projects/REVIX_LINK_ESP32` inside WSL. None flashes hardware.

## Build and test

```sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_bike
bash test/run.sh
```

C++ tests run with address/undefined-behavior sanitizers and warnings as errors. They cover all 65,536 raw RPM values, standard/extended IDs, DLC, RTR, endianness, mapping bounds, conversion overflow, zero/missing/stale/recovery, timer rollover, LED thresholds, TX/RX ordering, mismatches and the one-shot timeout latch.

## Wokwi TWAI test

Set `REVIX_MODE` to `REVIX_TWAI_TEST` in `include/config.h`, build, then manually start Wokwi. Replay is currently selected following the user's reproduced TX stall. `wokwi.toml` always points to `esp32dev_wokwi`. The diagram is edited directly; no helper rewrites it.

Demonstration frames use standard ID `0x555`, DLC 8, RPM as LE16 in bytes 0-1, increasing LE32 sequence in bytes 2-5, and `A5 5A` in bytes 6-7. These are not verified motorcycle protocol values. The send interval is 100 ms, with only one outstanding transaction.

`TWAI VERIFIED matched=100` requires 100 matching received frames, 100 TX_SUCCESS events and cleared pending TX. A received frame alone cannot pass. Diagnostics report alert mask, queues, TX/RX counts, TEC/REC, bus errors and receive loss. A timeout prints one `CAN HALTED` reason and stops the controller. After one final periodic stopped-state report, status logging becomes quiet; no repeated timeout or recovery loop occurs.

Save the complete Wokwi terminal output when investigating. Continuous successful TWAI operation in Wokwi has not been verified. See [the investigation](CAN-INVESTIGATION.md).

## Frame replay

Change the setting to `REVIX_REPLAY`, build and restart Wokwi. The 20-second sequence includes valid frames (0-12 s), wrong DLC (12-13 s), out-of-range values (13-14 s), silence (14-17 s), and recovery (17-20 s). This tests parsing/state/UI, not the TWAI controller, interrupts, ACKs or a physical bus.

Invalid frames do not overwrite RPM or extend its 1000 ms freshness deadline. Zero is valid; missing/stale values display `--`. LEDs turn off without fresh validated state. Six thresholds are 1084, 2167, 3250, 4334, 5417 and 6500 demonstration RPM; pairs are green, yellow and red. OLED updates are capped at 10 Hz, skipping unchanged output.

## Previous diagnostics

The minimal CAN-only probe, retry comparison, optional SocketCAN test and former preparation scripts are preserved outside the active project at:

`/home/hrik/project-archives/REVIX_LINK_ESP32/2026-10-25/workflow-before-simplification`

They are not needed to build or run this project. The former configuration and documentation are preserved there too. Older sketches and logs are in its parent directory.

Linux `vcan` is useful for host protocol tests, but does not exercise ESP32 TWAI or physical CAN. It is optional and not part of the Wokwi workflow. See the [kernel SocketCAN documentation](https://docs.kernel.org/networking/can.html).
