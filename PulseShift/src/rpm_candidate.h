// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "can_frame.h"

// ============================================================================
// Authoritative live engine-RPM decoder and state holder.
//
// This class is the SINGLE live RPM pipeline used by every subsystem: BLE
// telemetry, the unified LED controller, OLED/serial diagnostics and the
// mobile-assisted calibration workflow. No competing RPM value is maintained.
//
// The decoder is fully phone-configurable: arbitration ID, frame type, byte
// offset, byte width (1 or 2), byte order, multiplier, offset and validity
// clamp. Scaling remains strictly UNVERIFIED until the mobile calibration
// workflow binds a verification record to the exact decoder fingerprint.
//
// Physical RPM-driven shift lights remain DISABLED for any unverified
// configuration. The device is a passive listener: TX is software-muted and
// physically air-gapped.
// ============================================================================

// One atomic decoder configuration. Any change rebinds (invalidates) the
// calibration verification record because the fingerprint changes.
struct RpmDecoderSettings {
  uint32_t canId = 0x301;
  bool     extended = false;
  uint8_t  byteIndex = 0;      // first byte of the field (0..7)
  uint8_t  byteWidth = 1;      // 1 or 2
  bool     littleEndian = true;// byte order when byteWidth == 2
  float    multiplier = 40.0f; // RPM = raw * multiplier + offset
  float    offset = 0.0f;      // signed offset in RPM
  uint16_t maxRpm = 12000;     // validity clamp; above => invalid sample
};

// The single authoritative live RPM state consumed by every subsystem.
struct LiveRpmSnapshot {
  // Decoded value
  bool     available = false;        // at least one accepted frame since (re)configure
  bool     fresh = false;            // accepted frame within freshness timeout
  bool     valid = false;            // fresh && decoded RPM within [0, maxRpm]
  bool     engineRunning = false;    // valid && rpm > 0
  uint16_t rpm = 0;                  // decoded RPM; 0 when invalid
  uint16_t raw = 0;                  // extracted raw candidate value

  // Source frame evidence
  uint32_t canId = 0;
  bool     extended = false;
  uint8_t  dlc = 0;
  uint8_t  frameData[8] = {};

  // Timing
  uint32_t lastUpdateMs = 0;         // timestamp of last accepted frame
  uint32_t frameAgeMs = 0;           // now - lastUpdateMs (0xFFFFFFFE when never)
  float    updateRateHz = 0.0f;      // measured acceptance rate

  // Decoder settings echo (what produced this value)
  float    multiplier = 0.0f;
  float    offset = 0.0f;
  uint8_t  byteIndex = 0;
  uint8_t  byteWidth = 1;
  bool     littleEndian = true;

  // Calibration / verification
  bool     calibrationVerified = false;
  uint32_t calibrationFingerprint = 0;
  bool     calibrationMatchesSettings = false;

  // Health counters (per decoder configuration)
  uint32_t acceptedFrames = 0;
  uint32_t rejectedFrames = 0;
  uint32_t invalidSamples = 0;       // fresh frames whose decoded RPM was out of clamp
  uint32_t staleTransitions = 0;     // fresh -> stale observations
};

// Free-standing fingerprint of a decoder configuration so the configuration
// layer can compute fingerprints without depending on a monitor instance.
inline uint32_t computeDecoderFingerprint(const RpmDecoderSettings& s) {
  uint8_t buf[22];
  size_t o = 0;
  buf[o++] = static_cast<uint8_t>(s.canId & 0xFF);
  buf[o++] = static_cast<uint8_t>((s.canId >> 8) & 0xFF);
  buf[o++] = static_cast<uint8_t>((s.canId >> 16) & 0xFF);
  buf[o++] = static_cast<uint8_t>((s.canId >> 24) & 0xFF);
  buf[o++] = s.extended ? 1 : 0;
  buf[o++] = s.byteIndex;
  buf[o++] = s.byteWidth;
  buf[o++] = s.littleEndian ? 1 : 0;
  const int32_t multX1000 = static_cast<int32_t>(s.multiplier * 1000.0f + (s.multiplier >= 0 ? 0.5f : -0.5f));
  buf[o++] = static_cast<uint8_t>(multX1000 & 0xFF);
  buf[o++] = static_cast<uint8_t>((multX1000 >> 8) & 0xFF);
  buf[o++] = static_cast<uint8_t>((multX1000 >> 16) & 0xFF);
  buf[o++] = static_cast<uint8_t>((multX1000 >> 24) & 0xFF);
  const int32_t offX1000 = static_cast<int32_t>(s.offset * 1000.0f + (s.offset >= 0 ? 0.5f : -0.5f));
  buf[o++] = static_cast<uint8_t>(offX1000 & 0xFF);
  buf[o++] = static_cast<uint8_t>((offX1000 >> 8) & 0xFF);
  buf[o++] = static_cast<uint8_t>((offX1000 >> 16) & 0xFF);
  buf[o++] = static_cast<uint8_t>((offX1000 >> 24) & 0xFF);
  buf[o++] = static_cast<uint8_t>(s.maxRpm & 0xFF);
  buf[o++] = static_cast<uint8_t>((s.maxRpm >> 8) & 0xFF);

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < o; ++i) {
    crc ^= buf[i];
    for (uint8_t j = 0; j < 8; ++j) {
      if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
      else crc >>= 1;
    }
  }
  return crc ^ 0xFFFFFFFF;
}

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
  // Returns true if the frame matched the configured decoder source and was accepted.
  bool ingest(const CanFrame& frame, uint32_t nowMs);

  // ---- Atomic reconfiguration (mobile-configurable decoder) ----
  // Applies a complete decoder setting set in one step:
  //  - resets all live decode state (no old value can trigger LEDs),
  //  - invalidates the cached calibration verification mirror,
  //  - keeps cumulative counters bound to the previous configuration cleared.
  void applyDecoderSettings(const RpmDecoderSettings& settings, uint32_t nowMs);
  const RpmDecoderSettings& decoderSettings() const { return settings_; }

  // Stable fingerprint of the exact decoder configuration. A calibration
  // verification record is only meaningful while this value is unchanged.
  uint32_t decoderFingerprint() const;

  // ---- Status accessors ----
  uint32_t candidateId() const { return settings_.canId; }
  bool isExtended() const { return settings_.extended; }
  uint8_t byteIndex() const { return settings_.byteIndex; }
  uint8_t byteWidth() const { return settings_.byteWidth; }
  bool isLittleEndian() const { return settings_.littleEndian; }
  float offset() const { return settings_.offset; }
  float multiplier() const { return settings_.multiplier; }
  uint16_t maxRpm() const { return settings_.maxRpm; }

  // Legacy field-by-field setters. Each change invalidates calibration and
  // resets live decode state (they funnel into applyDecoderSettings).
  void setByteIndex(uint8_t idx);
  void setCandidateSource(uint32_t id, uint8_t byteIdx, bool extended);
  void setMultiplier(float m);
  void setByteWidth(uint8_t width);
  void setLittleEndian(bool le);
  void setOffset(float off);
  void setMaxRpm(uint16_t maxRpm);

  uint32_t freshnessTimeoutMs() const { return freshnessTimeoutMs_; }
  void setFreshnessTimeoutMs(uint32_t timeoutMs) { if (timeoutMs > 0) freshnessTimeoutMs_ = timeoutMs; }

  // Extracted raw candidate value (low byte for compatibility with v1 clients).
  uint8_t rawValue() const { return static_cast<uint8_t>(rawValue16_ & 0xFF); }
  uint16_t rawValue16() const { return rawValue16_; }
  uint16_t estimatedRpm(uint32_t nowMs) const;
  bool isFresh(uint32_t nowMs) const;
  uint32_t lastUpdateAgeMs(uint32_t nowMs) const;
  uint32_t lastUpdateTimestamp() const { return lastUpdateMs_; }
  float updateRateHz() const { return updateRateHz_; }

  uint32_t receivedCount() const { return receivedCount_; }
  uint32_t rejectedCount() const { return rejectedCount_; }
  uint32_t invalidSampleCount() const { return invalidSamples_; }
  uint32_t staleTransitionCount() const { return staleTransitions_; }
  uint16_t minObservedRaw() const { return minObservedRaw_; }
  uint16_t maxObservedRaw() const { return maxObservedRaw_; }
  bool hasReceivedFrame() const { return receivedCount_ > 0; }

  // Explicit safety constraints
  bool isCalibrated() const { return calibrationVerified_; }
  // Cached verification mirror; refreshed by the application from the
  // authoritative ConfigManager record on boot, commit and apply.
  void setCalibrationVerified(bool v) { calibrationVerified_ = v; }
  bool isShiftLightEnabled() const { return calibrationVerified_; }
  bool isTxIsolated() const { return true; }           // Compatibility alias: software mutes TX
  bool isTxDisabledInSoftware() const { return true; } // CAN TX software muted; physical isolation must be verified manually

  // Authoritative snapshot for all consumers (LED controller, BLE, OLED).
  LiveRpmSnapshot snapshot(uint32_t nowMs) const;

  // Formatting helpers for Arduino Serial Monitor
  void printStatus(uint32_t nowMs, bool controllerHealthy, uint32_t bitrate, bool busTrafficActive) const;
  void printStreamLine(uint32_t nowMs) const;

  void reset();

 private:
  RpmDecoderSettings settings_{};
  uint32_t freshnessTimeoutMs_;
  bool calibrationVerified_ = false;

  uint16_t rawValue16_ = 0;
  uint32_t lastUpdateMs_ = 0;
  bool hasUpdate_ = false;

  uint32_t receivedCount_ = 0;
  uint32_t rejectedCount_ = 0;
  uint32_t invalidSamples_ = 0;
  uint32_t staleTransitions_ = 0;
  uint16_t minObservedRaw_ = 0xFFFF;
  uint16_t maxObservedRaw_ = 0;

  float updateRateHz_ = 0.0f;
  uint32_t prevAcceptMs_ = 0;

  // Last matching frame evidence (for the snapshot's source-frame fields)
  uint8_t lastFrameDlc_ = 0;
  uint8_t lastFrameData_[8] = {};

  static bool validMultiplier(float m) { return !isnan(m) && !isinf(m) && m > 0.0f && m <= 1000.0f; }
};
