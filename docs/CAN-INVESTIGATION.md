# TWAI investigation — 25 October 2026

## What is established

The supplied log is evidence from the user's original run, not a new run by this agent. It reports running state, one queued TX, one received frame, zero completed TX, pending TX=1, and zero reported error counters. That proves one RX delivery and an unretired transmit request. It does not prove a complete CAN transaction or identify the missing hardware/simulator event.

The original `checkCANHealth()` tests `txPending && elapsed >= timeout` without checking `txStopped`. Setting `txStopped` does not clear `txPending`. `faultReported` protects only the following status block. Thus every later loop prints the timeout again. This is a definite application bug. New code uses a terminal fault latch, prints the pre-stop snapshot once, stops sending, and performs no recovery retries.

## New user-run reproduction

The user subsequently ran the simplified dashboard firmware in Wokwi and supplied this excerpt (whitespace normalized):

```text
REVIX LINK mode=TWAI_SELF_TEST SDK=v4.4.7-dirty
RX t=612 id=00000555 ext=0 rtr=0 dlc=8 data=000000000000A55A VALID
CAN status=ESP_OK state=1 pendingTX=1 pendingRX=0 TEC=0 REC=0 failed=0 busErrors=0 missed=0 overrun=0 arbLost=0 queued=1 completed=0 RX=1 matched=0 alerts=0x00000004 halted=0
CAN HALTED: TX completion timeout
CAN STOP: ESP_OK
CAN status=ESP_OK state=0 pendingTX=0 pendingRX=0 TEC=0 REC=0 failed=0 busErrors=0 missed=0 overrun=0 arbLost=0 queued=1 completed=0 RX=1 matched=0 alerts=0x00000004 halted=1
```

The installed v4.4.7 header defines RX_DATA=0x4 and TX_SUCCESS=0x2. Thus this run delivered a self-received frame but no recorded TX-success alert; the driver still had one pending TX before stop. Clearing pending TX through `twai_stop()` is not completion. The payload is the first demonstration frame: zero RPM and sequence zero, not evidence of motorcycle RPM. The timeout was emitted once, and later status lines showed the controller stayed stopped. This reproduces the stall with the current application safeguards, without proving its precise simulator/HAL cause.

The supplied terminal output also showed staircase indentation. Application printf/raw frame lines used LF alone; they now use CRLF. Periodic logging now ends after one final stopped-state report. Those presentation fixes were compiled, but not rerun in Wokwi by the agent. Replay was explicitly selected for further parser/dashboard testing; no automatic fallback or fabricated TX success was added.

## Driver evidence

In Espressif's [v4.4.7 driver](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/twai.c), the TX handler decrements `tx_msg_count`; the RX handler does not. `msgs_to_tx` includes the active controller buffer even with the software TX queue disabled. Losing only the application's alert would not by itself explain a permanently pending driver count. RX and TX notifications are separate; the ISR processes the RX event first when both occur. Applications must tolerate either observed order. The log is consistent with failure to process TX completion, but cannot distinguish simulator behavior, configuration or driver/HAL interaction.

The earlier decimal error 263 (`0x107`, ESP_ERR_TIMEOUT) fits failure to obtain space in an enabled software TX queue. With that driver's queue disabled and its TX buffer occupied, the enqueue branch returns ESP_FAIL instead. Therefore the older queue-timeout version and this queue-disabled sketch should not be conflated. Increasing queue size only postpones saturation if completion stalls.

## Controller configuration and wiring

[Espressif's TWAI guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/twai.html) defines NO_ACK as allowing transmission without another node's acknowledgement, and SELF as requesting receipt of the transmitted message. Enqueue success is not completion. These settings are a legitimate self-test combination; they do not promise an internal electrical loopback.

The [official self-test example](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/peripherals/twai/twai_self_test/main/twai_self_test_example_main.c) uses NO_ACK plus SELF, an external transceiver, 25 kbit/s and ordinary retry behavior. REVIX's probe deliberately retains the original 500 kbit/s and single-shot setting first, with a second build changing only single-shot. It strengthens the acceptance criterion by checking both TX completion and the echoed sequence.

A local digital GPIO17→GPIO16 mirror is appropriate as a simulator loopback experiment; it does not simulate a differential automotive bus or validate a transceiver. The [DevKit V1 pin map](https://github.com/wokwi/wokwi-boards/blob/main/boards/esp32-devkit-v1/board.json) names those pins TX2/RX2. D17/D16 in the archived diagram are not published endpoints; the current root diagram uses TX2/RX2. The latest user run still stalls with this corrected diagram. No controlled wire-removal comparison has been captured.

## Simulator support: bounded conclusion

[Wokwi's feature matrix](https://docs.wokwi.com/guides/esp32) labels TWAI as partial implementation/work in progress. It does not separately promise full NO_ACK, SELF, TX completion interrupt and alert fidelity. The linked [Wokwi test workflow](https://github.com/wokwi/esp32-test-binaries/actions/workflows/test.yml) is not evidence that this exact self-test passed.

Partial simulation is a plausible contributor, not a proven root cause. The new user-supplied log reproduces the application/driver completion stall in Wokwi, but does not isolate a particular missing simulator interrupt. Continuous TWAI success is not established. Current test settings and strict acceptance criteria are in TESTING.md; the standalone probe and comparison configurations are archived as documented there. No success alerts are synthesized and no direct RPM path is substituted for CAN decoding.

## Hardware errata and passive reception

Espressif documents a [lost transmit interrupt erratum](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32/03-errata-description/esp32/twai-transmit-interrupt-lost.html). It is an alternative mechanism, not proof of what Wokwi implements. The installed Arduino ESP32 SDK enables `CONFIG_TWAI_ERRATA_FIX_TX_INTR_LOST`.

That SDK does **not** enable `CONFIG_TWAI_ERRATA_FIX_LISTEN_ONLY_DOM`. The [HAL implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/twai_hal.c) conditionally forces passive error signaling when this option is built in. The new receive-only firmware additionally disables the TX pad before start through [GPIO configuration](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gpio.c). This requires a verified external recessive TXD bias and preferably transceiver silent mode. Hardware measurements remain mandatory; no bench behavior has been claimed.
