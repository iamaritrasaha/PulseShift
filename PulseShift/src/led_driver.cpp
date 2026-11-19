// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_driver.h"

#if defined(ARDUINO) && defined(ESP32)
#include <Arduino.h>

bool Esp32RmtLedDriver::begin(uint8_t pin, uint8_t count) {
  if (isTerminalFault()) return false;
  if (count == 0 || count > MAX_PIXELS || (ready_ && (pin != pin_ || count != count_))) {
    lastStatus_ = LedDriverStatus::InvalidParam;
    return false;
  }
  if (ready_) return true;

  pin_ = pin;
  count_ = count;

  if (!rmtInit(pin_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_4, 10000000)) {
    lastStatus_ = LedDriverStatus::InitFailed;
    return false;
  }
  if (!rmtSetEOT(pin_, 0)) {
    lastStatus_ = LedDriverStatus::InitFailed;
    releaseIdleChannel();
    return false;
  }

  ready_ = true;
  lastStatus_ = LedDriverStatus::Ready;
  return true;
}

void Esp32RmtLedDriver::releaseIdleChannel() {
  ready_ = false;
  if (!rmtDeinit(pin_)) {
    // 3.3.12 may have freed the bus while leaving its pin mapping in place.
    // Stop touching that mapping if the detach operation failed.
    lastStatus_ = LedDriverStatus::DeinitFailed;
  }
}

bool Esp32RmtLedDriver::write(const RgbColor* pixels, uint8_t count) {
  if (isTerminalFault()) return false;
  if (!ready_ || !pixels || count != count_) {
    lastStatus_ = LedDriverStatus::InvalidParam;
    return false;
  }
  if (txPending_ && !rmtTransmitCompleted(pin_)) {
    if (uint32_t(millis() - txStartedMs_) >= TRANSMIT_STALL_MS) {
      // Arduino 3.3.12 deinit frees callback resources before disabling TX.
      // On ESP32 that disable can wait indefinitely for a hardware TX_DONE.
      // Quarantine the channel and its borrowed symbols until system restart.
      ready_ = false;
      lastStatus_ = LedDriverStatus::TransmitStalled;
    } else {
      lastStatus_ = LedDriverStatus::Busy;
    }
    return false;
  }
  txPending_ = false;

  // GRB, most significant bit first. At 10 MHz each tick is 100 ns.
  size_t length = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t grb[] = {pixels[i].g, pixels[i].r, pixels[i].b};
    for (uint8_t channel : grb) {
      for (int8_t bit = 7; bit >= 0; --bit) {
        const bool one = (channel >> bit) & 1;
        symbols_[length].level0 = 1;
        symbols_[length].duration0 = one ? 8 : 3;
        symbols_[length].level1 = 0;
        symbols_[length].duration1 = one ? 6 : 9;
        ++length;
      }
    }
  }
  // Encode 300 us low in every frame so fast updates still latch WS2812B-V5.
  symbols_[length].level0 = symbols_[length].level1 = 0;
  symbols_[length].duration0 = symbols_[length].duration1 = 1500;
  ++length;

  // A synchronous HAL write conflates a rejected enqueue with a timeout. On
  // immediate rejection the HAL may leave TX_DONE cleared with no TX pending.
  if (!rmtWriteAsync(pin_, symbols_, length)) {
    lastStatus_ = LedDriverStatus::TransmitRejected;
    releaseIdleChannel();
    return false;
  }
  txPending_ = true;
  txStartedMs_ = millis();
  lastStatus_ = LedDriverStatus::Busy;
  return true;
}

LedDriverStatus Esp32RmtLedDriver::pollTransmission() {
  if (!txPending_ || isTerminalFault()) return lastStatus_;
  if (rmtTransmitCompleted(pin_)) {
    txPending_ = false;
    lastStatus_ = LedDriverStatus::TransmitOk;
  } else {
    const uint32_t elapsed = millis() - txStartedMs_;
    if (elapsed >= TRANSMIT_STALL_MS) {
      // A late or missing TX_DONE does not release the HAL's borrowed buffer.
      ready_ = false;
      lastStatus_ = LedDriverStatus::TransmitStalled;
    } else if (elapsed >= TRANSMIT_TIMEOUT_MS) {
      lastStatus_ = LedDriverStatus::Timeout;
    }
  }
  return lastStatus_;
}

const char* Esp32RmtLedDriver::lastStatusString() const {
  switch (lastStatus_) {
    case LedDriverStatus::Ready: return "READY";
    case LedDriverStatus::TransmitOk: return "TRANSMIT_OK";
    case LedDriverStatus::Timeout: return "TIMEOUT";
    case LedDriverStatus::InitFailed: return "INIT_FAILED";
    case LedDriverStatus::InvalidParam: return "INVALID_PARAM";
    case LedDriverStatus::Busy: return "BUSY";
    case LedDriverStatus::TransmitRejected: return "TRANSMIT_REJECTED";
    case LedDriverStatus::DeinitFailed: return "DEINIT_FAILED";
    case LedDriverStatus::TransmitStalled: return "TX_STALLED_RESTART_REQUIRED";
    default: return "UNINITIALIZED";
  }
}
#endif
