// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "can_transport.h"
#include "diag_logger.h"
#include <Arduino.h>
#include <sdkconfig.h>
#include <driver/gpio.h>

bool CanTransport::begin() {
  if (beginAttempted_) return healthy();
  beginAttempted_ = true;

  if (!configured()) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("CAN DISABLED: no verified bitrate/profile configured");
#endif
    DiagLogger::instance().logCanInit(0, -1, "NOT_CONFIGURED");
    return false;
  }

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  constexpr auto mode = TWAI_MODE_NO_ACK;
#else
  constexpr auto mode = TWAI_MODE_LISTEN_ONLY;
#endif

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      gpio_num_t(config::txPin), gpio_num_t(config::rxPin), mode);
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  general.tx_queue_len = 5;
#else
  general.tx_queue_len = 0;
#endif
  general.rx_queue_len = 256;
  general.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF |
                           TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN |
                           TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN;

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  general.alerts_enabled |= TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_TX_IDLE;
#endif

  twai_timing_config_t timing{};
  switch (bitrate_) {
    case 125000: timing = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 250000: timing = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 500000: timing = TWAI_TIMING_CONFIG_500KBITS(); break;
    case 1000000: timing = TWAI_TIMING_CONFIG_1MBITS(); break;
    default:
      DiagLogger::instance().logCanInit(bitrate_, -2, "UNSUPPORTED_BITRATE");
      fault("unsupported bitrate");
      return false;
  }

  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  auto err = twai_driver_install(&general, &timing, &filter);
  installed_ = (err == ESP_OK);

#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_TWAI_TEST)
  // Ensure the ESP32 TX pin (GPIO17) is isolated in input/pull-up mode
  // while the controller is running in listen-only mode.
  // Note: The physical hardware installation keeps ESP32 GPIO17 physically
  // disconnected, while transceiver CTX/TXD is tied directly to 3V3.
  if (err == ESP_OK) err = gpio_reset_pin(gpio_num_t(config::txPin));
  if (err == ESP_OK) err = gpio_set_direction(gpio_num_t(config::txPin), GPIO_MODE_INPUT);
  if (err == ESP_OK) err = gpio_set_pull_mode(gpio_num_t(config::txPin), GPIO_PULLUP_ONLY);
  if (err == ESP_OK) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("PASSIVE CAPTURE: GPIO%d muted (input/pull-up); verify CTX->3V3 and GPIO17 physically disconnected\r\n", config::txPin);
#endif
  }
#endif

  if (err == ESP_OK) err = twai_start();
  running_ = (err == ESP_OK);

  DiagLogger::instance().logCanInit(bitrate_, static_cast<int>(err), esp_err_to_name(err));

  if (!running_) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("CAN INIT: %s\r\n", esp_err_to_name(err));
#endif
    fault("initialization failed");
  }

  return running_;
}

void CanTransport::fault(const char* reason) {
  if (!tracker_.halt()) return;
  DiagLogger::instance().logCanHalted(reason);

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  if (log_) log_->status("CAN HALTED: %s", reason);
  else Serial.printf("CAN HALTED: %s\r\n", reason);
  diagnostics();
#else
  (void)reason;
#endif

  if (running_) {
    const auto err = twai_stop();
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    if (log_) log_->status("CAN STOP: %s", esp_err_to_name(err));
    else Serial.printf("CAN STOP: %s\r\n", esp_err_to_name(err));
#else
    (void)err;
#endif
  }
  running_ = false; // Manual reboot required. No automatic recovery loop.
}

void CanTransport::diagnostics() const {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  if (!installed_) return;
  twai_status_info_t s{};
  auto err = twai_get_status_info(&s);
  if (log_) {
    log_->status("CAN status=%s state=%d pendingTX=%lu pendingRX=%lu halted=%d",
      esp_err_to_name(err), int(s.state), (unsigned long)s.msgs_to_tx,
      (unsigned long)s.msgs_to_rx, tracker_.halted());
    log_->status("CAN TEC=%lu REC=%lu failed=%lu busErrors=%lu",
      (unsigned long)s.tx_error_counter, (unsigned long)s.rx_error_counter,
      (unsigned long)s.tx_failed_count, (unsigned long)s.bus_error_count);
    log_->status("CAN missed=%lu overrun=%lu arbLost=%lu",
      (unsigned long)s.rx_missed_count, (unsigned long)s.rx_overrun_count, (unsigned long)s.arb_lost_count);
    log_->status("CAN RX=%lu acceptedFrames=%lu busErrorAlerts=%lu",
      (unsigned long)rx_, (unsigned long)acceptedRx_, (unsigned long)busErrorAlerts_.total());
    return;
  }
  Serial.printf("CAN status=%s state=%d pendingTX=%lu pendingRX=%lu TEC=%lu REC=%lu failed=%lu busErrors=%lu missed=%lu overrun=%lu arbLost=%lu queued=%lu completed=%lu RX=%lu acceptedFrames=%lu matched=%lu alerts=0x%08lX busErrorAlerts=%lu rxOverflowAlerts=%lu halted=%d\r\n",
    esp_err_to_name(err), int(s.state), (unsigned long)s.msgs_to_tx, (unsigned long)s.msgs_to_rx,
    (unsigned long)s.tx_error_counter, (unsigned long)s.rx_error_counter, (unsigned long)s.tx_failed_count,
    (unsigned long)s.bus_error_count, (unsigned long)s.rx_missed_count, (unsigned long)s.rx_overrun_count,
    (unsigned long)s.arb_lost_count, (unsigned long)queued_, (unsigned long)completed_,
    (unsigned long)rx_, (unsigned long)acceptedRx_, (unsigned long)matched_, (unsigned long)alertsSeen_,
    (unsigned long)busErrorAlerts_.total(), (unsigned long)rxOverflowAlerts_.total(), tracker_.halted());
#endif
}

void CanTransport::poll(uint32_t now) {
  if (!healthy()) return;

  uint32_t alerts = 0;
  auto err = twai_read_alerts(&alerts, 0);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) { fault("alert read failed"); return; }
  alertsSeen_ |= alerts;

  // Fatal hardware bus-off state
  if (alerts & TWAI_ALERT_BUS_OFF) {
    fault("bus-off detected");
    return;
  }

  // Non-fatal RX overflow: record and rate-limit warnings without halting
  if (alerts & (TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN)) {
    rxOverflowAlerts_.record();
  }
  const auto overflowWarnings = rxOverflowAlerts_.take(now, 1000);
  if (overflowWarnings) {
    DiagLogger::instance().logEvent(DiagLevel::Warn, "CAN_RX_OVERFLOW",
                                   "rx queue full or FIFO overrun count=%lu total=%lu",
                                   (unsigned long)overflowWarnings,
                                   (unsigned long)rxOverflowAlerts_.total());
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    if (log_) log_->status("CAN WARNING rxOverflowAlerts=%lu total=%lu",
                           (unsigned long)overflowWarnings, (unsigned long)rxOverflowAlerts_.total());
    else Serial.printf("CAN WARNING: RX overflow alerts=%lu total=%lu\r\n",
                       (unsigned long)overflowWarnings, (unsigned long)rxOverflowAlerts_.total());
#endif
  }

#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_TWAI_TEST)
  if (alerts & TWAI_ALERT_BUS_ERROR) busErrorAlerts_.record();
  const auto warnings = busErrorAlerts_.take(now, 1000);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  if (warnings && log_) log_->status("CAN WARNING busErrorAlerts=%lu total=%lu",
    (unsigned long)warnings, (unsigned long)busErrorAlerts_.total());
  else if (warnings) Serial.printf("CAN WARNING: bus-error alerts=%lu total=%lu (last interval; see TEC/REC/busErrors)\r\n",
    (unsigned long)warnings, (unsigned long)busErrorAlerts_.total());
#else
  (void)warnings;
#endif

  // Track inactivity
  if (rx_ > 0 && wasActive_ && uint32_t(now - lastRxMs_) >= 5000) {
    wasActive_ = false;
    DiagLogger::instance().logCanInactivity(now - lastRxMs_);
  }
#else
  if (alerts & TWAI_ALERT_BUS_ERROR) {
    fault("bus error in self-test; inspect counters"); return;
  }
#endif

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  if (alerts & TWAI_ALERT_TX_FAILED) { fault("TX failed"); return; }
  if (alerts & TWAI_ALERT_TX_SUCCESS) {
    if (!tracker_.onTxSuccess()) { fault("unexpected TX success"); return; }
    ++completed_;
  }
  twai_status_info_t s{};
  if (twai_get_status_info(&s) != ESP_OK) { fault("status read failed"); return; }
  if (tracker_.finish(s.msgs_to_tx)) {
    ++matched_;
    if (matched_ % 100 == 0) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("TWAI VERIFIED matched=%lu\r\n", (unsigned long)matched_);
#endif
    }
  }
  if (tracker_.timedOut(now, config::txTimeoutMs)) fault(tracker_.txSeen() ? "RX timeout" : "TX completion timeout");
#else
  twai_status_info_t status{};
  if (twai_get_status_info(&status) != ESP_OK || status.state != TWAI_STATE_RUNNING || status.msgs_to_tx != 0) {
    fault("controller status failure");
    return;
  }

  // Monitor dropped frames and report non-fatal overflows
  if (status.rx_missed_count != lastMissedCount_ || status.rx_overrun_count != lastOverrunCount_) {
    const uint32_t newMissed = (status.rx_missed_count >= lastMissedCount_) ? (status.rx_missed_count - lastMissedCount_) : status.rx_missed_count;
    const uint32_t newOverrun = (status.rx_overrun_count >= lastOverrunCount_) ? (status.rx_overrun_count - lastOverrunCount_) : status.rx_overrun_count;
    if (newMissed > 0 || newOverrun > 0) {
      DiagLogger::instance().logEvent(DiagLevel::Warn, "CAN_RX_OVERFLOW",
                                     "rx overflow missed=%lu overrun=%lu (new missed=%lu overrun=%lu)",
                                     (unsigned long)status.rx_missed_count,
                                     (unsigned long)status.rx_overrun_count,
                                     (unsigned long)newMissed,
                                     (unsigned long)newOverrun);
    }
  }

  lastBusErrorCount_ = status.bus_error_count;
  lastMissedCount_ = status.rx_missed_count;
  lastOverrunCount_ = status.rx_overrun_count;
#endif
}

bool CanTransport::receive(CanFrame& f, uint32_t now) {
  if (!healthy()) return false;
  twai_message_t msg{};
  auto err = twai_receive(&msg, 0);
  if (err == ESP_ERR_TIMEOUT) return false;
  if (err != ESP_OK) { fault("RX driver failure"); return false; }

  f = {};
  f.id = msg.identifier;
  f.dlc = msg.data_length_code;
  f.extended = msg.extd;
  f.remote = msg.rtr;
  f.timestampMs = now;
  if (!f.remote) memcpy(f.data, msg.data, f.dlc <= 8 ? f.dlc : 8);
  f.payloadSize = f.remote ? 0 : (f.dlc <= 8 ? f.dlc : 8);

  lastRxMs_ = now;
  if (!wasActive_) {
    wasActive_ = true;
    DiagLogger::instance().logCanResume(rx_ + 1);
  }

  ++rx_;
  if (validEnvelope(f)) ++acceptedRx_;

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  if (!tracker_.onReceive(f)) { fault("RX payload/sequence mismatch"); return false; }
#endif

  return true;
}

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
bool CanTransport::sendTest(const CanFrame& f, uint32_t now) {
  if (!canSend() || !validEnvelope(f) || f.remote) return false;
  twai_message_t msg{};
  msg.identifier = f.id; msg.extd = f.extended; msg.data_length_code = f.dlc;
  msg.self = 1; msg.ss = 1; memcpy(msg.data, f.data, f.dlc);
  const auto err = twai_transmit(&msg, 0);
  if (err != ESP_OK) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("TX enqueue: %s\r\n", esp_err_to_name(err));
#endif
    fault("TX enqueue failed"); return false;
  }
  tracker_.begin(f, now); ++queued_; return true;
}
#endif
