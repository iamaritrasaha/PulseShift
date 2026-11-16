// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_driver.h"

#if defined(ARDUINO) && defined(ESP32)
#include <Arduino.h>
#include "esp32-hal-rmt.h"

bool Esp32RmtLedDriver::begin(uint8_t pin, uint8_t count) {
  pin_ = pin;
  count_ = count;

  // Initialize RMT channel with 4 memory blocks (256 symbols) at 10 MHz resolution (100 ns tick).
  // 6 WS2812B LEDs require 6 * 24 = 144 symbols, which fits comfortably inside the 256-symbol hardware RAM,
  // preventing intermediate interrupt refilling and eliminating ISR latency underruns.
  if (!rmtInit(pin_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_4, 10000000)) {
    ready_ = false;
    lastStatus_ = LedDriverStatus::InitFailed;
    return false;
  }

  // Ensure line idles LOW after transmission (WS2812B latch/reset requirement)
  rmtSetEOT(pin_, 0);

  ready_ = true;
  lastStatus_ = LedDriverStatus::Ready;
  return true;
}

bool Esp32RmtLedDriver::write(const RgbColor* pixels, uint8_t count) {
  if (!ready_ || !pixels || count == 0) {
    lastStatus_ = LedDriverStatus::InvalidParam;
    return false;
  }

  // 24 symbols per WS2812B LED (GRB order, MSB first)
  const size_t numSymbols = static_cast<size_t>(count) * 24;
  rmt_data_t symbols[numSymbols];

  size_t symIdx = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t g = pixels[i].g;
    const uint8_t r = pixels[i].r;
    const uint8_t b = pixels[i].b;

    // Green byte
    for (int8_t bit = 7; bit >= 0; --bit) {
      if ((g >> bit) & 1) {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 8; // 800 ns
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 4; // 400 ns
      } else {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 4; // 400 ns
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 8; // 800 ns
      }
      symIdx++;
    }

    // Red byte
    for (int8_t bit = 7; bit >= 0; --bit) {
      if ((r >> bit) & 1) {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 8;
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 4;
      } else {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 4;
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 8;
      }
      symIdx++;
    }

    // Blue byte
    for (int8_t bit = 7; bit >= 0; --bit) {
      if ((b >> bit) & 1) {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 8;
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 4;
      } else {
        symbols[symIdx].level0 = 1;
        symbols[symIdx].duration0 = 4;
        symbols[symIdx].level1 = 0;
        symbols[symIdx].duration1 = 8;
      }
      symIdx++;
    }
  }

  // Transmit via Arduino RMT HAL with a 20 ms timeout (frame takes ~180 us)
  const bool ok = rmtWrite(pin_, symbols, numSymbols, 20);
  if (ok) {
    lastStatus_ = LedDriverStatus::TransmitOk;
    return true;
  } else {
    lastStatus_ = LedDriverStatus::Timeout;
    return false;
  }
}

const char* Esp32RmtLedDriver::lastStatusString() const {
  switch (lastStatus_) {
    case LedDriverStatus::Ready: return "READY";
    case LedDriverStatus::TransmitOk: return "TRANSMIT_OK";
    case LedDriverStatus::Timeout: return "TIMEOUT";
    case LedDriverStatus::InitFailed: return "INIT_FAILED";
    case LedDriverStatus::InvalidParam: return "INVALID_PARAM";
    case LedDriverStatus::Busy: return "BUSY";
    default: return "UNINITIALIZED";
  }
}
#endif
