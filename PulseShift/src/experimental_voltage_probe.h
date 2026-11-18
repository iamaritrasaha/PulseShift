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
    if (frame.extended || frame.remote || !validEnvelope(frame)) return;

    switch (frame.id) {
      case kCanId: voltage_.ingest(frame, kByteIndex, nowMs); break;
      case 0x302:
        diagnostics_[0].ingest(frame, 1, nowMs);
        diagnostics_[1].ingest(frame, 5, nowMs);
        break;
      case 0x452: diagnostics_[2].ingest(frame, 7, nowMs); break;
      case 0x540: diagnostics_[3].ingest(frame, 3, nowMs); break;
    }
  }

  bool isFresh(uint32_t nowMs) const {
    return voltage_.isFresh(nowMs);
  }

  // Piggybacks on the two previously reserved BLE RPM packet bytes:
  // little-endian low byte = raw 0x303[1] (only valid when high=0xA5);
  // high byte 0xA5 = fresh, 0x5A = supported but absent/stale.
  // Old firmware emits 0x0000, so Android can distinguish unsupported.
  uint16_t bleReservedWord(uint32_t nowMs) const {
    if (!isFresh(nowMs)) return static_cast<uint16_t>(kStaleTag) << 8;
    return (static_cast<uint16_t>(kFreshTag) << 8) | voltage_.value;
  }

  // TLV 0x4A: freshness mask, followed by four raw bytes in CAN field order.
  void diagnosticsSnapshot(uint32_t nowMs, uint8_t (&out)[5]) const {
    out[0] = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      const bool fresh = diagnostics_[i].isFresh(nowMs);
      if (fresh) out[0] |= 1u << i;
      out[1 + i] = fresh ? diagnostics_[i].value : 0;
    }
  }

 private:
  struct Sample {
    uint32_t lastSeenMs = 0;
    uint8_t value = 0;
    bool seen = false;

    void ingest(const CanFrame& frame, uint8_t index, uint32_t nowMs) {
      if (frame.dlc <= index) return;
      value = frame.data[index];
      lastSeenMs = nowMs;
      seen = true;
    }

    bool isFresh(uint32_t nowMs) const {
      return seen && uint32_t(nowMs - lastSeenMs) <= kFreshnessMs;
    }
  };

  Sample voltage_;
  Sample diagnostics_[4];
};
