// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "can_transport.h"
#include <Arduino.h>
#include <sdkconfig.h>
#include <driver/gpio.h>
bool CanTransport::begin() {
  if (!configured()) { Serial.println("CAN DISABLED: set verified REVIX_BITRATE before bench capture"); return false; }
#if REVIX_MODE == REVIX_BIKE
  constexpr auto mode = TWAI_MODE_LISTEN_ONLY;
#else
  constexpr auto mode = TWAI_MODE_NO_ACK;
#endif
  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t(config::txPin), gpio_num_t(config::rxPin), mode);
  general.tx_queue_len = 0;
  general.rx_queue_len = 64;
  general.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL |
    TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_RX_DATA;
#if REVIX_MODE == REVIX_TWAI_TEST
  general.alerts_enabled |= TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_TX_IDLE;
#endif
  twai_timing_config_t timing{};
  switch (REVIX_BITRATE) {
    case 125000: timing = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 250000: timing = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 500000: timing = TWAI_TIMING_CONFIG_500KBITS(); break;
    case 1000000: timing = TWAI_TIMING_CONFIG_1MBITS(); break;
    default: fault("unsupported bitrate"); return false;
  }
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  auto err = twai_driver_install(&general, &timing, &filter);
  installed_ = err == ESP_OK;
#if REVIX_MODE == REVIX_BIKE
  // Arduino 2.0.17's prebuilt SDK lacks LISTEN_ONLY_DOM workaround. Disconnect
  // the TX pad while the controller is still STOPPED. Keep it disconnected for
  // the entire passive capture; no code here ever re-enables that output.
  // Bench hardware must hold transceiver TXD recessive with an external pull-up
  // and preferably assert the transceiver's silent mode as well.
  if (err == ESP_OK) err = gpio_reset_pin(gpio_num_t(config::txPin));
  if (err == ESP_OK) err = gpio_set_direction(gpio_num_t(config::txPin), GPIO_MODE_INPUT);
  if (err == ESP_OK) err = gpio_set_pull_mode(gpio_num_t(config::txPin), GPIO_PULLUP_ONLY);
  if (err == ESP_OK) Serial.println("PASSIVE CAPTURE: GPIO17 output disabled; external TXD pull-up required");
#endif
  if (err == ESP_OK) err = twai_start();
  running_ = err == ESP_OK;
  if (!running_) { Serial.printf("CAN INIT: %s\r\n", esp_err_to_name(err)); fault("initialization failed"); }
  return running_;
}
void CanTransport::fault(const char* reason) {
  if (!tracker_.halt()) return;
  Serial.printf("CAN HALTED: %s\r\n", reason);
  diagnostics();
  if (running_) Serial.printf("CAN STOP: %s\r\n", esp_err_to_name(twai_stop()));
  running_ = false; // Manual reboot required. No automatic recovery loop.
}
void CanTransport::diagnostics() const {
  if (!installed_) return;
  twai_status_info_t s{};
  auto err = twai_get_status_info(&s);
  Serial.printf("CAN status=%s state=%d pendingTX=%lu pendingRX=%lu TEC=%lu REC=%lu failed=%lu busErrors=%lu missed=%lu overrun=%lu arbLost=%lu queued=%lu completed=%lu RX=%lu matched=%lu alerts=0x%08lX halted=%d\r\n",
    esp_err_to_name(err), int(s.state), (unsigned long)s.msgs_to_tx, (unsigned long)s.msgs_to_rx,
    (unsigned long)s.tx_error_counter, (unsigned long)s.rx_error_counter, (unsigned long)s.tx_failed_count,
    (unsigned long)s.bus_error_count, (unsigned long)s.rx_missed_count, (unsigned long)s.rx_overrun_count,
    (unsigned long)s.arb_lost_count, (unsigned long)queued_, (unsigned long)completed_,
    (unsigned long)rx_, (unsigned long)matched_, (unsigned long)alertsSeen_, tracker_.halted());
}
void CanTransport::poll(uint32_t now) {
  if (!healthy()) return;
  uint32_t alerts = 0;
  auto err = twai_read_alerts(&alerts, 0);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) { fault("alert read failed"); return; }
  alertsSeen_ |= alerts;
  if (alerts & (TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN)) {
    fault("bus-off or RX overflow; inspect counters"); return;
  }
#if REVIX_MODE == REVIX_BIKE
  // In listen-only mode, isolated bus errors are non-fatal: the CAN controller
  // recovers automatically and an automotive bus may produce transient errors at
  // startup or under electrical disturbance. Count them for diagnostics.
  if (alerts & TWAI_ALERT_BUS_ERROR) {
    Serial.println("CAN WARNING: bus error detected (non-fatal in listen-only mode)");
  }
#else
  if (alerts & TWAI_ALERT_BUS_ERROR) {
    fault("bus error in self-test; inspect counters"); return;
  }
#endif
#if REVIX_MODE == REVIX_TWAI_TEST
  if (alerts & TWAI_ALERT_TX_FAILED) { fault("TX failed"); return; }
  if (alerts & TWAI_ALERT_TX_SUCCESS) {
    if (!tracker_.onTxSuccess()) { fault("unexpected TX success"); return; }
    ++completed_;
  }
  twai_status_info_t s{};
  if (twai_get_status_info(&s) != ESP_OK) { fault("status read failed"); return; }
  if (tracker_.finish(s.msgs_to_tx)) {
    ++matched_;
    if (matched_ % 100 == 0) Serial.printf("TWAI VERIFIED matched=%lu\r\n", (unsigned long)matched_);
  }
  if (tracker_.timedOut(now, config::txTimeoutMs)) fault(tracker_.txSeen() ? "RX timeout" : "TX completion timeout");
#else
  (void)now;
#endif
}
bool CanTransport::receive(CanFrame& f, uint32_t now) {
  if (!healthy()) return false;
  twai_message_t msg{};
  auto err = twai_receive(&msg, 0);
  if (err == ESP_ERR_TIMEOUT) return false;
  if (err != ESP_OK) { fault("RX driver failure"); return false; }
  f = {}; f.id = msg.identifier; f.dlc = msg.data_length_code;
  f.extended = msg.extd; f.remote = msg.rtr; f.timestampMs = now;
  if (!f.remote) memcpy(f.data, msg.data, f.dlc <= 8 ? f.dlc : 8);
  ++rx_;
#if REVIX_MODE == REVIX_TWAI_TEST
  if (!tracker_.onReceive(f)) { fault("RX payload/sequence mismatch"); return false; }
#endif
  return true;
}
#if REVIX_MODE == REVIX_TWAI_TEST
bool CanTransport::sendTest(const CanFrame& f, uint32_t now) {
  if (!canSend() || !validEnvelope(f) || f.remote) return false;
  twai_message_t msg{};
  msg.identifier = f.id; msg.extd = f.extended; msg.data_length_code = f.dlc;
  msg.self = 1; msg.ss = 1; memcpy(msg.data, f.data, f.dlc);
  const auto err = twai_transmit(&msg, 0);
  if (err != ESP_OK) { Serial.printf("TX enqueue: %s\r\n", esp_err_to_name(err)); fault("TX enqueue failed"); return false; }
  tracker_.begin(f, now); ++queued_; return true;
}
#endif
