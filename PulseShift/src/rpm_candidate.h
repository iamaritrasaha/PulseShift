// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "can_frame.h"

// Diagnostic-only candidate engine-speed monitor for passive CAN exploration.
// Candidate ID: 0x301 (standard 11-bit), Byte index: 0.
// Important: Scaling is a provisional hypothesis and remains strictly UNVERIFIED.
// Physical shift lights remain strictly DISABLED for this candidate.
class RpmCandidateMonitor {
 public:
  static constexpr uint32_t DEFAULT_CANDIDATE_ID = 0x301;
  static constexpr uint8_t DEFAULT_BYTE_INDEX = 0;
  static constexpr float DEFAULT_MULTIPLIER = 40.0f;
  // In real capture, 0x301 broadcasts at ~40-50 Hz (every 20-25 ms).
  // 250 ms represents ~10-12 consecutive missing frames before declaring stale.
  static constexpr uint32_t DEFAULT_FRESHNESS_TIMEOUT_MS = 250;

  explicit RpmCandidateMonitor(uint32_t candidateId = DEFAULT_CANDIDATE_ID,
                               uint8_t byteIndex = DEFAULT_BYTE_INDEX,
                               float multiplier = DEFAULT_MULTIPLIER,
                               uint32_t freshnessTimeoutMs = DEFAULT_FRESHNESS_TIMEOUT_MS);

  // Ingest incoming CAN frame from the TWAI receive pipeline.
  // Returns true if the frame matched candidate arbitration ID and passed validation.
  bool ingest(const CanFrame& frame, uint32_t nowMs);

  // Status accessors
  uint32_t candidateId() const { return candidateId_; }
  uint8_t byteIndex() const { return byteIndex_; }
  void setByteIndex(uint8_t idx) { if (idx < 8) byteIndex_ = idx; }
  float multiplier() const { return multiplier_; }
  void setMultiplier(float m) {
    if (!isnan(m) && !isinf(m) && m > 0.0f && m <= 1000.0f) {
      multiplier_ = m;
    }
  }

  uint32_t freshnessTimeoutMs() const { return freshnessTimeoutMs_; }
  void setFreshnessTimeoutMs(uint32_t timeoutMs) { if (timeoutMs > 0) freshnessTimeoutMs_ = timeoutMs; }

  uint8_t rawValue() const { return rawValue_; }
  uint16_t estimatedRpm(uint32_t nowMs) const;
  bool isFresh(uint32_t nowMs) const;
  uint32_t lastUpdateAgeMs(uint32_t nowMs) const;
  uint32_t lastUpdateTimestamp() const { return lastUpdateMs_; }

  uint32_t receivedCount() const { return receivedCount_; }
  uint32_t rejectedCount() const { return rejectedCount_; }
  uint8_t minObservedRaw() const { return minObservedRaw_; }
  uint8_t maxObservedRaw() const { return maxObservedRaw_; }
  bool hasReceivedFrame() const { return receivedCount_ > 0; }

  // Explicit safety constraints
  bool isCalibrated() const { return false; }          // Calibration is UNVERIFIED
  bool isShiftLightEnabled() const { return false; }   // Shift lights DISABLED
  bool isTxIsolated() const { return true; }           // Compatibility alias: software mutes TX
  bool isTxDisabledInSoftware() const { return true; } // CAN TX software muted; physical isolation must be verified manually

  // Formatting helpers for Arduino Serial Monitor
  void printStatus(uint32_t nowMs, bool controllerHealthy, uint32_t bitrate, bool busTrafficActive) const;
  void printStreamLine(uint32_t nowMs) const;

  void reset();

 private:
  uint32_t candidateId_;
  uint8_t byteIndex_;
  float multiplier_;
  uint32_t freshnessTimeoutMs_;

  uint8_t rawValue_ = 0;
  uint32_t lastUpdateMs_ = 0;

  uint32_t receivedCount_ = 0;
  uint32_t rejectedCount_ = 0;
  uint8_t minObservedRaw_ = 0xFF;
  uint8_t maxObservedRaw_ = 0;
};
