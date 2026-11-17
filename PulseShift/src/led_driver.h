// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "led_color.h"

enum class LedDriverStatus : uint8_t {
  Uninitialized = 0,
  Ready = 1,
  TransmitOk = 2,
  Busy = 3,
  Timeout = 4,
  InitFailed = 5,
  InvalidParam = 6,
  TransmitRejected = 7,
  DeinitFailed = 8
};

// Abstract interface for the physical or simulated LED output driver.
// Allows decoupling pixel calculation and transmission arbitration from
// hardware transport quirks, enabling deterministic mock testing.
class ILedDriver {
 public:
  virtual ~ILedDriver() = default;
  virtual bool begin(uint8_t pin, uint8_t count) = 0;
  virtual bool write(const RgbColor* pixels, uint8_t count) = 0;
  virtual bool isReady() const = 0;
  virtual LedDriverStatus lastStatus() const = 0;
  virtual const char* lastStatusString() const = 0;
};

#if defined(ARDUINO) && defined(ESP32)
#include <esp32-hal-rmt.h>
// Native ESP32 RMT driver utilizing Arduino-ESP32 HAL (esp32-hal-rmt).
// Four ESP32 RMT blocks hold a six-pixel frame, latch interval and HAL EOF.
class Esp32RmtLedDriver : public ILedDriver {
 public:
  Esp32RmtLedDriver() = default;
  ~Esp32RmtLedDriver() override = default;

  bool begin(uint8_t pin, uint8_t count) override;
  bool write(const RgbColor* pixels, uint8_t count) override;
  bool isReady() const override { return ready_; }
  LedDriverStatus lastStatus() const override { return lastStatus_; }
  const char* lastStatusString() const override;

 private:
  void releaseIdleChannel();

  static constexpr uint8_t MAX_PIXELS = 6;
  static constexpr size_t BITS_PER_PIXEL = 24;
  static constexpr uint32_t TRANSMIT_TIMEOUT_MS = 20;
  // IDF may retain this payload after a completion timeout.
  rmt_data_t symbols_[MAX_PIXELS * BITS_PER_PIXEL + 1]{};
  uint8_t pin_ = 0;
  uint8_t count_ = 0;
  bool ready_ = false;
  bool txPending_ = false;
  bool deinitFailed_ = false;
  LedDriverStatus lastStatus_ = LedDriverStatus::Uninitialized;
};
#endif

// Deterministic mock LED driver for host tests, CI, and test injection.
class MockLedDriver : public ILedDriver {
 public:
  MockLedDriver() = default;
  ~MockLedDriver() override = default;

  bool begin(uint8_t pin, uint8_t count) override {
    pin_ = pin;
    count_ = count;
    if (failBeginCount_ > 0) {
      --failBeginCount_;
      ready_ = false;
      lastStatus_ = LedDriverStatus::InitFailed;
      return false;
    }
    ready_ = true;
    lastStatus_ = LedDriverStatus::Ready;
    return true;
  }

  bool write(const RgbColor* pixels, uint8_t count) override {
    ++writeAttempts_;
    if (!ready_ || !pixels || count == 0) {
      lastStatus_ = LedDriverStatus::InvalidParam;
      return false;
    }
    if (failNextCount_ > 0) {
      --failNextCount_;
      lastStatus_ = LedDriverStatus::Timeout;
      return false;
    }
    for (uint8_t i = 0; i < count && i < 6; ++i) {
      lastWrittenPixels_[i] = pixels[i];
    }
    ++successfulWrites_;
    lastStatus_ = LedDriverStatus::TransmitOk;
    return true;
  }

  bool isReady() const override { return ready_; }
  LedDriverStatus lastStatus() const override { return lastStatus_; }
  const char* lastStatusString() const override {
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

  void setFailNextWrites(uint32_t count) { failNextCount_ = count; }
  void setFailBegin(uint32_t count) { failBeginCount_ = count; }
  uint32_t writeAttempts() const { return writeAttempts_; }
  uint32_t successfulWrites() const { return successfulWrites_; }
  RgbColor lastWrittenPixel(uint8_t idx) const { return (idx < 6) ? lastWrittenPixels_[idx] : RgbColor{0, 0, 0}; }

 private:
  uint8_t pin_ = 0;
  uint8_t count_ = 0;
  bool ready_ = false;
  LedDriverStatus lastStatus_ = LedDriverStatus::Uninitialized;
  uint32_t failNextCount_ = 0;
  uint32_t failBeginCount_ = 0;
  uint32_t writeAttempts_ = 0;
  uint32_t successfulWrites_ = 0;
  RgbColor lastWrittenPixels_[6]{};
};
