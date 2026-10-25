// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
struct CanFrame {
  uint32_t id = 0;
  uint32_t timestampMs = 0; // local dequeue/replay time, NOT hardware bus timestamp
  uint8_t dlc = 0;
  bool extended = false, remote = false;
  uint8_t data[8]{};
};
inline bool validEnvelope(const CanFrame& f) {
  return f.dlc <= 8 && f.id <= (f.extended ? 0x1FFFFFFFUL : 0x7FFUL);
}
