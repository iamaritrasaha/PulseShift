# PulseShift Android BLE Terminal: Integration Contract

This is the implementation guide for the **existing** Kotlin / Jetpack Compose /
Material 3 app at `D:\Development\PulseShift\android`. The Android source is
not stored in this firmware GitHub repository. This file documents the real
firmware contract; it is **not** a claim that the app UI is already implemented.

## Goal

Add a **Terminal** destination to the existing navigation: a scrollable
monospace command transcript, one-line prompt, Help / clear / copy controls,
command history (up/down or history sheet), and connection/authorization status.
The interface must show **real ESP32 responses, never fabricated success**.
Disabled commands while disconnected should explain why. Keep the rest of the
Material 3 app, BLE state and repository architecture intact.

Do NOT try to send ASCII serial-console commands directly into GATT. The
serial parser on the ESP32 only handles USB Serial. The Android terminal is a
human-readable client for the versioned BLE protocol below.

## Commands and exact dispatch mapping

| Terminal input | Execution over BLE |
| --- | --- |
| `HELP`, `?` | Local command registry; show syntax, authorization, availability |
| `STATUS`, `CAN STATUS` | `GET_DIAGNOSTICS` (`0x50`), decode TLV blocks and CRC |
| `BLE STATUS` | `GET_DIAGNOSTICS` (`0x50`), show BLE state, firmware |
| `RPM STATUS` | Live v1 RPM notification and `GET_DIAGNOSTICS` TLV candidate |
| `RPM MONITOR ON` | Subscribe to v1 RPM notifications, format at <= 10 Hz |
| `RPM MONITOR OFF` | Stop terminal rendering, preserve subscriptions required by other screens |
| `RPM MONITOR` | Show latest real RPM telemetry sample, with age/staleness |
| `CONFIG STATUS` | `GET_SNAPSHOT` (`0x02`), validate CRC32 and show current values |
| `CONFIG SAVE` | `SAVE_CONFIG` (`0x20`); require ACK with PERSISTED flag |
| `CONFIG RESET` | Explicit confirmation, then `RESTORE_DEFAULTS` (`0x21`, payload `A5 5A`) |
| `RPM MULTIPLIER <m>` | `SET_RPM_MULTIPLIER` (`0x18`, little-endian `round(m*1000)` as u32), no auto-save |
| `EFFECT <0..5>` | `SET_EFFECT` (`0x14`, payload `[effectId,speed]`). Effect 5 is **UNVERIFIED** live preview; effect 4 requires bound calibration |
| `RPM TEST <0..20000>` | `SET_LED_TEST_RPM` (`0x55`, `[rpm u16LE, 1]`), expires after 10 seconds |
| `RPM TEST OFF` | `SET_LED_TEST_RPM` (`0x55`, `[0,0,0]`) |
| `STAGE <1|2|3>` | `SET_CAPTURE_STAGE` (`0x54`, `[stage]`), affects subsequent CAN capture labels only |
| `CAPTURE STOP`, `CAPTURE PAUSE` | `SET_CAPTURE_RECORDING` (`0x53`, `[0]`) |
| `CAPTURE RESUME`, `CAPTURE START` | `SET_CAPTURE_RECORDING` (`0x53`, `[1]`), can be rejected if flash preserved/full/damaged |
| `LOGS` | v1 `GET_LOG_PAGE` (`0x10`), stream bounded pages, CRC/sequence checks |
| `DUMP`, `DUMP NEXT` | v1 `GET_CAPTURE_PAGE` (`0x12`), resume from phone cursor |
| `DUMP PAGE <n>` | Convert page index to BLE record offset (12 records per BLE page) |
| `DUMP ALL` | Sequential BLE pages with ACK/backpressure; allow cancel; **never** one unbounded request |
| `DUMP CANCEL` | v1 `CANCEL_CAPTURE` (`0x13`); discard phone cursor as selected |
| `RESTART` | Require confirmation, then `RESTART_DEVICE` (`0x51`, `A5 5A`) |
| `CLEAR DIAGNOSTICS` | Require confirmation, then `CLEAR_DIAGNOSTICS` (`0x52`, `A5 5A`). Does **not** erase CAN captures |
| `AUTH` | Display physical ESP32 BOOT-button onboarding instructions; use existing secure owner-enrollment/challenge flow |
| `STREAM ON/OFF` | USB-only serial CAN live stream. Print `UNSUPPORTED OVER BLE` instead of pretending to stream |
| `CLEAR YES` | USB-only irreversible capture wipe. **Do not** expose destructive file deletion through a generic terminal command |

Do not invent aliases or mark unsupported operations as completed. Extend the
catalogue only when a matching firmware BLE command and tests exist. `RPM
MULTIPLIER` and other decoder edits invalidate any verified calibration.
Changing values in the current session is **not** the same as persisting them;
show a "Not saved" indicator until `CONFIG SAVE` returns PERSISTED.

## New firmware BLE opcodes

Both use the existing v2 **20-byte** command/result GATT characteristics. No
raw terminal socket, UART bridge or new service is required.

- `0x54 SET_CAPTURE_STAGE`: authenticated, payload length **1**, stage 1..3.
  Response: matched sequence + `OK` + `APPLIED`; invalid values rejected.
- `0x55 SET_LED_TEST_RPM`: authenticated, payload length **3**,
  `[u16 little-endian rpm, u8 enabled]`, `rpm <= 20000`, enabled 0 or 1.
  Response: matched sequence + `OK` + `APPLIED`; a 10-second nonpersistent
  bench-test LED request. `enabled=0` stops it.
- Feature bits `BLE_FEAT_CAPTURE_STAGE=1<<15` and
  `BLE_FEAT_LED_BENCH_TEST=1<<16` advertise support. Check before showing
  each command as available; older 2.2.0 devices won't support them.
- `SET_EFFECT` accepts **0..5**, with 5 as provisional/unverified CAN preview.

## App implementation guidelines

- Inspect current `MainActivity.kt`, navigation, `BlePulseShiftRepository.kt`,
  `PulseShiftBleManager.kt` and `PulseShiftBleProtocol.kt` first.
- Add an explicit `TerminalCommandRegistry` with a sealed parsed-command model,
  typed numeric range validation, and a command-help catalogue sourced from the
  same definitions as dispatch. Commands are case-insensitive; whitespace is
  normalized, but do not perform fuzzy execution for dangerous operations.
- Use the existing BLE command serializer and ACK/result stream. Track the
  **sequence number**, opcode, status code, applied/staged/persisted flags,
  revision and timestamps. Serialize write requests, respect the bounded
  firmware queue, and use timeouts plus retry only for safe/idempotent queries.
- Never send privileged writes without a connection-bound authorized session.
  The physical BOOT window is required for first owner enrollment; no bypasses.
- Parse diagnostic TLV, snapshot chunking and CRC32; print readable fields.
  Preserve `UNVERIFIED` on RPM values. Show `RPM LED preview` separately from
  verified `RPM Bar`.
- Bound terminal transcript in memory (e.g. 500 lines), support copy/export to
  file, and never log owner secrets, challenge-response proof, BLE tokens or
  sensitive pairing state.
- A single terminal-owned flow subscription for `RPM MONITOR` should not disrupt
  the home dashboard subscriptions. Stop or suspend terminal output offscreen.
- For `DUMP`, preserve existing captured data, use BLE-record pagination (12
  records/request), CRC checks, cancellation, gap detection and backpressure.
- Destructive commands always require a confirmation dialog. An unrelated
  serial command must not magically appear as a successful BLE action.

## Required tests

1. Parser: `HELP`, whitespace, uppercase/lowercase, negative/overflow/
   malformed parameters, unknown commands.
2. BLE dispatch: matching ACK sequence/opcode, success, rejected permissions,
   timeout, disconnect and no fake success.
3. `RPM TEST 4500`, `RPM TEST OFF`, invalid RPM, both on supported and older
   devices that omit the relevant capabilities bit.
4. `STAGE 2` readback via capture diagnostics, reject stage 0/4.
5. `RPM MULTIPLIER 42.5` updates live config, flags unsaved state, then
   `CONFIG SAVE` persists; reboot readback. Never silently mark calibrated.
6. `CONFIG RESET` and `CLEAR DIAGNOSTICS` confirmation; captures and owner
   credentials remain untouched.
7. `DUMP ALL` is bounded, cancelable, resumable; handle disconnect and missing
   pages with an error, not a green success log.
8. Device tests on the actual Android phone and ESP32: connection, BOOT
   authorization, RPM notifications, LED bench test, stage label change,
   storage, power cycle and reconnect.

## Scope and limitations

The firmware BLE dispatcher and host tests are implemented in this repository.
**Android app implementation and Android device testing are pending because
the app exists only at the separate local Windows workspace.**
