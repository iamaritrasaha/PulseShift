// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "rpm_decoder.h"
enum class ViewStatus { Fault, Unconfigured, Disconnected, Unmapped, Invalid, Stale, Live };
inline ViewStatus viewStatus(const RpmState& s, uint32_t now, uint32_t timeout, bool healthy, bool configured = true) {
  if (!configured) return ViewStatus::Unconfigured;
  if (!healthy) return ViewStatus::Fault;
  if (s.fresh(now, timeout)) return ViewStatus::Live;
  if (s.signalInvalid()) return ViewStatus::Invalid;
  if (s.hasValue()) return ViewStatus::Stale;
  if (!s.busFresh(now, timeout)) return ViewStatus::Disconnected;
  return s.mapped() ? ViewStatus::Invalid : ViewStatus::Unmapped;
}
inline uint8_t litLedCount(uint16_t rpm, uint16_t maximum, uint8_t count, bool valid) {
  if (!valid || maximum == 0) return 0;
  if (rpm >= maximum) return count;
  return uint8_t(uint32_t(rpm) * count / maximum);
}
