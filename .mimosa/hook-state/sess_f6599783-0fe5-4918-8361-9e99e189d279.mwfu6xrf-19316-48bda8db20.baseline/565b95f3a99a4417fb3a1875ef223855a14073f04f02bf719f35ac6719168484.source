// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>

// Aggregate alert observations without printing on every poll. Driver hardware
// counters remain separate: one alert can represent multiple bus errors.
class EventSummary {
 public:
  void record() { ++total_; ++pending_; }
  uint32_t take(uint32_t now, uint32_t interval) {
    if (!pending_ || uint32_t(now - lastReport_) < interval) return 0;
    lastReport_ = now;
    const uint32_t count = pending_; pending_ = 0; return count;
  }
  uint32_t total() const { return total_; }
 private:
  uint32_t total_ = 0, pending_ = 0, lastReport_ = 0;
};
