// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "simulated_ecu.h"
#include "config.h"

#if REVIX_MODE == REVIX_REPLAY || REVIX_MODE == REVIX_TWAI_TEST
RpmMapping demonstrationMapping() {
  RpmMapping m;
  m.enabled = true;
  m.id = 0x555;
  m.maxRpm = config::maxRpm;
  return m;
}

CanFrame demonstrationFrame(uint32_t now, uint32_t sequence) {
  CanFrame f;
  f.id = 0x555;
  f.dlc = 8;
  f.timestampMs = now;
  const uint32_t step = sequence % 131;
  const uint32_t fraction = step <= 65 ? step : 130 - step;
  const uint16_t raw = uint16_t(fraction * config::maxRpm / 65);
  f.data[0] = uint8_t(raw);
  f.data[1] = uint8_t(raw >> 8);
  for (unsigned i = 0; i < 4; ++i) f.data[i + 2] = uint8_t(sequence >> (8 * i));
  f.data[6] = 0xA5;
  f.data[7] = 0x5A;
  return f;
}

bool ReplayTransport::receive(uint32_t now, CanFrame& f) {
  if (uint32_t(now - last_) < 100) return false;
  last_ = now;

  if (paused_) return false;

  if (manualRpm_ >= 0) {
    f = {}; // Clear flags/payload when reusing a caller-owned frame.
    f.id = 0x555;
    f.dlc = 8;
    f.timestampMs = now;
    // Preserve out-of-range injection so the shared decoder can reject it.
    const uint16_t raw = static_cast<uint16_t>(manualRpm_);
    f.data[0] = uint8_t(raw);
    f.data[1] = uint8_t(raw >> 8);
    for (unsigned i = 0; i < 4; ++i) f.data[i + 2] = uint8_t(sequence_ >> (8 * i));
    f.data[6] = 0xA5;
    f.data[7] = 0x5A;
    ++sequence_;
    return true;
  }

  // Every 20s: acceleration -> shift point -> deceleration -> malformed -> out-of-range -> silence -> recovery.
  const uint32_t phase = now % 20000;
  if (phase >= 14000 && phase < 17000) return false;
  f = demonstrationFrame(now, sequence_++);
  if (phase >= 12000 && phase < 13000) f.dlc = 1;
  if (phase >= 13000 && phase < 14000) { f.data[0] = 0xFF; f.data[1] = 0xFF; }
  return true;
}
#endif
