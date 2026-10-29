# Wokwi TWAI investigation

Status on 2026-10-27: **TX completion FAILS in the tested Wokwi setup. Physical
CAN has not been tested.** The existing esp32dev_twai_test environment, its
transmitter and its completion checks remain enabled and unchanged.

Recorded observations predate the PulseShift rename; original banners and hashes are retained.

## Reproduction

Run these in Ubuntu WSL from the repository root:

~~~sh
~/.platformio/penv/bin/pio run -e esp32dev_twai_test
python3 test/build_wokwi_twai_probes.py --arduino-cli /home/hrik/.local/bin/arduino-cli --output-dir .pio/pr3-review/probes
~~~

Use Wokwi's [documented custom-firmware template](https://wokwi.com/projects/305457271083631168).
Paste test/wokwi_twai_probe.diagram.json into its diagram editor, then use the
editor command "Upload Firmware and Start Simulation" to upload each
.pio/pr3-review/probes/VARIANT/build/twai_probe.ino.elf. This is an unsaved browser
test; do not publish it or flash these transmitting probes onto vehicle hardware.

The minimal circuit contains only ESP32, UART and GPIO17 (TX2) to GPIO16 (RX2).
The probe sends one artificial ID 0x555 frame, drains RX and alerts without
blocking, reports every 500ms, then stops once after 2500ms. It has no OLED,
LED driver, RPM parser, application logger, TX tracker or recovery loop.

| Variant | Bitrate | Single shot | TX queue | DLC | RX | Completed | Pending TX |
| --- | --- | --- | --- | --- | --- | --- | --- |
| single-shot | 500000 | 1 | 0 | 8 | 1 | 0 | 1 |
| retry-enabled | 500000 | 0 | 0 | 8 | 1 | 0 | 1 |
| queued | 500000 | 1 | 5 | 8 | 1 | 0 | 1 |
| reference-aligned | 25000 | 0 | 5 | 1 | 1 | 0 | 1 |

All use TWAI_MODE_NO_ACK and self reception. The final row adopts the rate,
default queue size, one-byte payload and self flag from Espressif's example.
It is a bounded single-frame reduction, not an execution of the unmodified
FreeRTOS example. The first three rows change one relevant setting at a time.

An additional negative control removed ONLY the TX2-to-RX2 connection from the
diagram and re-uploaded the single-shot ELF. It still produced the same RX and
incomplete TX. The diagram editor was inspected to confirm that the connection
was absent. Thus a self-received frame in this simulator does not even establish
that the external digital loopback connection was exercised.

[Recorded observations](fixtures/wokwi_twai_observations.json) contain the actual
UART lines, source hash and ELF hashes. All variants report alerts=0x4
(TWAI_ALERT_RX_DATA), no TX_SUCCESS/TX_FAILED/TX_IDLE, TEC=REC=0 and no bus errors.
The read-only status register stays SR=0x00, including BEFORE transmission;
TBS and TCS remain zero. IER=0xE7 includes TX interrupt enable. No probe reads
the interrupt register, because reading it could consume interrupt flags.

## Explanation and limits

[Wokwi's support table](https://docs.wokwi.com/guides/esp32) labels TWAI partial
implementation/work in progress. The controlled observations are consistent
with incomplete TX completion/status emulation or an emulator/driver interaction.
They rule out the dashboard workload, disabled software TX queue and single-shot
flag as sufficient explanations. They do not identify a particular internal
Wokwi defect; its internal peripheral implementation was not inspected. Zero
emulated counters/status bits do not establish an electrically healthy CAN bus.

[Espressif's v4.4.7 API guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/twai.html)
defines NO_ACK as permitting successful transmission without a peer ACK, so
absence of another acknowledging node is not by itself an explanation here.
It also distinguishes successful enqueue from successful transmission.
[The official self-test](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/peripherals/twai/twai_self_test/main/twai_self_test_example_main.c)
uses NO_ACK plus self reception. A simulated GPIO loop supplies digital feedback,
but cannot substitute for a transceiver, differential bus or hardware qualification.

In the [v4.4.7 reference driver](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/twai.c),
transmit increments the driver's outstanding-message count. RX processing
decrements only the RX count. The TX-buffer-free handler reports success or
failure and decrements the TX count. Therefore RX can be observed while driver
TX is still pending, and receiving it must not clear pending TX in the app.
The observations indicate that this completion path is not making progress.
Blind repeated enqueue would eventually fill a configured queue and return
ESP_ERR_TIMEOUT (263). Increasing its size only postpones that symptom.

The application self-test requires matching RX, TX_SUCCESS and zero driver
pending TX before another transmission. Its current run received one matching
frame, timed out once, stopped once, emitted one stopped-state report and
remained quiet on subsequent observation. twai_stop clears pending TX as part of
stopping; that zero is not evidence of completed transmission. No fake completion,
queue-clearing workaround, disabled test or automatic restart was introduced.

## Acceptance boundary

The ordinary Wokwi demo remains complete-frame replay and passes its separate
runtime checks. It validates parser/state/LED behavior, not TWAI or physical CAN.
Receive-only images remain separately checked for absent transmit symbols.
A software preview can be reviewed with this documented simulator failure;
successful hardware CAN operation and motorcycle installation remain unapproved.
A future simulator version must be rerun with these probes, rather than treating
this recorded failure as permanent or changing the test to accept RX alone.
