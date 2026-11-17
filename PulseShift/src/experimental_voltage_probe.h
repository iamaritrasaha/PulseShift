// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "can_frame.h"
#include <stdint.h>

// EXPERIMENTAL, UNCALIBRATED vehicle signal probe. This does not modify
// the RPM decoder, transmit CAN, change NVS, or write to LittleFS.
//
// Prior key-on capture: standard CAN 0x303 data[1] was 0x77..0x7A.
// Hypothesis ONLY: each raw count equals 0.1 V. Validate on a stationary
// motorcycle against a synchronized multimeter before calling this voltage.
class ExperimentalVoltageProbe {
 public:
  static constexpr uint32_t kCanId = 0x303u;
  static constexpr uint8_t kByteIndex = 1u;
  static constexpr uint32_t kFreshnessMs = 1000u;
  static constexpr uint8_t kFreshTag = 0xA5u;
  static constexpr uint8_t kStaleTag = 0x5Au;

  static ExperimentalVoltageProbe& instance() {
    static ExperimentalVoltageProbe probe;
    return probe;
  }

  void ingest(const CanFrame& frame, uint32_t nowMs) {
    if (!validEnvelope(frame) || frame.extended || frame.remote ||
        frame.id != kCanId || frame.dlc <= kByteIndex) {
      return;
    }
    value_ = frame.data[kByteIndex];
    lastSeenMs_ = nowMs;
    seen_ = true;
  }

  bool isFresh(uint32_t nowMs) const {
    return seen_ && static_cast<uint32_t>(nowMs - lastSeenMs_) <= kFreshnessMs;
  }

  // Piggybacks on the two previously reserved BLE RPM packet bytes:
  // little-endian low byte = raw 0x303[1] (only valid when high=0xA5);
  // high byte 0xA5 = fresh, 0x5A = supported but absent/stale.
  // Old firmware emits 0x0000, so Android can distinguish unsupported.
  uint16_t bleReservedWord(uint32_t nowMs) const {
    if (!isFresh(nowMs)) return static_cast<uint16_t>(kStaleTag) << 8;
    return (static_cast<uint16_t>(kFreshTag) << 8) | value_;
  }

 private:
  uint32_t lastSeenMs_ = 0;
  uint8_t value_ = 0;
  bool seen_ = false;
};
