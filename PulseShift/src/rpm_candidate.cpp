// Copyright (c) 2026 Aritra Saha. All rights reserved.
// config.h pulls in UserConfig.h, so PULSESHIFT_SERIAL_DIAGNOSTICS is defined
// for this translation unit. Without it, the preprocessor silently compiles
// out all RPM serial output; fail loudly if the setting is unavailable.
#if defined(ARDUINO)
  #include <Arduino.h>
  #define RPM_PRINTF(...) Serial.printf(__VA_ARGS__)
  #define RPM_PRINTLN(x) Serial.println(x)
#elif defined(__has_include)
  #if __has_include(<Arduino.h>)
    #include <Arduino.h>
    #define RPM_PRINTF(...) Serial.printf(__VA_ARGS__)
    #define RPM_PRINTLN(x) printf("%s\r\n", (x))
  #else
    #define RPM_PRINTF(...) printf(__VA_ARGS__)
    #define RPM_PRINTLN(x) printf("%s\r\n", (x))
  #endif
#else
  #define RPM_PRINTF(...) printf(__VA_ARGS__)
  #define RPM_PRINTLN(x) printf("%s\r\n", (x))
#endif

#include "rpm_candidate.h"
#include "config.h"
#ifndef PULSESHIFT_SERIAL_DIAGNOSTICS
#error "RPM diagnostics setting is missing from UserConfig.h"
#endif
#include <stdio.h>
#include <string.h>

RpmCandidateMonitor::RpmCandidateMonitor(uint32_t candidateId,
                                         uint8_t byteIndex,
                                         float multiplier,
                                         uint32_t freshnessTimeoutMs)
    : freshnessTimeoutMs_(freshnessTimeoutMs > 0 ? freshnessTimeoutMs : DEFAULT_FRESHNESS_TIMEOUT_MS) {
  settings_.canId = candidateId;
  settings_.byteIndex = byteIndex < 8 ? byteIndex : 0;
  settings_.multiplier = validMultiplier(multiplier) ? multiplier : DEFAULT_MULTIPLIER;
}

uint32_t RpmCandidateMonitor::decoderFingerprint() const {
  return computeDecoderFingerprint(settings_);
}

void RpmCandidateMonitor::applyDecoderSettings(const RpmDecoderSettings& s, uint32_t nowMs) {
  RpmDecoderSettings next = s;
  // Clamp and sanitize every field; invalid input falls back to safe values.
  if (next.byteIndex >= 8) next.byteIndex = 0;
  if (next.byteWidth != 1 && next.byteWidth != 2) next.byteWidth = 1;
  if (next.byteIndex + next.byteWidth > 8) next.byteIndex = 0;
  if (next.extended) {
    if (next.canId > 0x1FFFFFFFUL) next.canId = 0x301;
  } else {
    if (next.canId > 0x7FFUL) next.canId = 0x301;
  }
  if (!validMultiplier(next.multiplier)) next.multiplier = DEFAULT_MULTIPLIER;
  if (isnan(next.offset) || isinf(next.offset)) next.offset = 0.0f;
  if (next.offset < -10000.0f) next.offset = -10000.0f;
  if (next.offset > 10000.0f) next.offset = 10000.0f;
  if (next.maxRpm == 0 || next.maxRpm > 20000) next.maxRpm = 12000;

  const bool sourceChanged = (settings_.canId != next.canId) ||
                             (settings_.extended != next.extended) ||
                             (settings_.byteIndex != next.byteIndex) ||
                             (settings_.byteWidth != next.byteWidth) ||
                             (settings_.littleEndian != next.littleEndian);

  settings_ = next;
  // Any decoder change invalidates the previous calibration verification and
  // prevents unverified RPM values from triggering LEDs.
  calibrationVerified_ = false;

  // Reset stale decoder state: if source frame or byte layout changed, old raw
  // data is completely meaningless and must be wiped immediately.
  if (sourceChanged) {
    rawValue16_ = 0;
    hasUpdate_ = false;
    minObservedRaw_ = 0xFFFF;
    maxObservedRaw_ = 0;
    memset(lastFrameData_, 0, sizeof(lastFrameData_));
    lastFrameDlc_ = 0;
    lastUpdateMs_ = nowMs;
  }
  // A multiplier/offset-only change keeps the previous raw sample and its
  // timestamp: it is honestly re-scaled, but it must NOT appear fresh for
  // another window when no frame has actually arrived.
  receivedCount_ = 0;
  rejectedCount_ = 0;
  invalidSamples_ = 0;
  staleTransitions_ = 0;
  updateRateHz_ = 0.0f;
  prevAcceptMs_ = 0;
}

void RpmCandidateMonitor::setByteIndex(uint8_t idx) {
  if (idx >= 8) return;
  RpmDecoderSettings s = settings_;
  s.byteIndex = idx;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setCandidateSource(uint32_t id, uint8_t byteIdx, bool extended) {
  if (byteIdx >= 8) return;
  RpmDecoderSettings s = settings_;
  s.canId = id;
  s.byteIndex = byteIdx;
  s.extended = extended;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setMultiplier(float m) {
  if (!validMultiplier(m)) return;
  RpmDecoderSettings s = settings_;
  s.multiplier = m;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setByteWidth(uint8_t width) {
  if (width != 1 && width != 2) return;
  RpmDecoderSettings s = settings_;
  s.byteWidth = width;
  if (s.byteIndex + s.byteWidth > 8) s.byteIndex = 0;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setLittleEndian(bool le) {
  RpmDecoderSettings s = settings_;
  s.littleEndian = le;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setOffset(float off) {
  if (isnan(off) || isinf(off) || off < -10000.0f || off > 10000.0f) return;
  RpmDecoderSettings s = settings_;
  s.offset = off;
  applyDecoderSettings(s, lastUpdateMs_);
}
void RpmCandidateMonitor::setMaxRpm(uint16_t maxRpm) {
  if (maxRpm == 0 || maxRpm > 20000) return;
  RpmDecoderSettings s = settings_;
  s.maxRpm = maxRpm;
  applyDecoderSettings(s, lastUpdateMs_);
}

bool RpmCandidateMonitor::ingest(const CanFrame& frame, uint32_t nowMs) {
  if (frame.id != settings_.canId) {
    return false;
  }

  // Reject invalid candidate frame envelopes:
  // Must match standard/extended expectation, not RTR, and DLC must be exactly 8 bytes
  // matching the Bajaj Pulsar NS125 broadcast specification.
  const bool expectExtended = settings_.extended || (settings_.canId > 0x7FFUL);
  if (frame.extended != expectExtended || frame.remote || frame.dlc != 8 || !validEnvelope(frame)) {
    ++rejectedCount_;
    return false;
  }

  // Safeguard array bounds: field must fully fit inside the payload
  if (settings_.byteIndex >= frame.dlc ||
      unsigned(settings_.byteIndex) + settings_.byteWidth > unsigned(frame.dlc) ||
      settings_.byteIndex >= 8) {
    ++rejectedCount_;
    return false;
  }

  // Extract the configured field width / byte order.
  uint16_t extracted = 0;
  if (settings_.byteWidth == 1) {
    extracted = frame.data[settings_.byteIndex];
  } else {
    const uint8_t a = frame.data[settings_.byteIndex];
    const uint8_t b = frame.data[settings_.byteIndex + 1];
    extracted = settings_.littleEndian ? static_cast<uint16_t>(a | (b << 8))
                                       : static_cast<uint16_t>((a << 8) | b);
  }

  rawValue16_ = extracted;
  // Count a signal dropout: this frame arrives after the freshness window
  // already expired, so RPM rendering would have been blanked in between.
  if (hasUpdate_ && uint32_t(nowMs - lastUpdateMs_) > freshnessTimeoutMs_) {
    ++staleTransitions_;
  }
  lastUpdateMs_ = nowMs;
  hasUpdate_ = true;
  lastFrameDlc_ = frame.dlc;
  memcpy(lastFrameData_, frame.data, sizeof(lastFrameData_));

  // Update-rate estimate (exponential moving average of instantaneous rate).
  if (prevAcceptMs_ != 0) {
    const uint32_t interval = uint32_t(nowMs - prevAcceptMs_);
    if (interval > 0 && interval < 5000) {
      const float inst = 1000.0f / static_cast<float>(interval);
      updateRateHz_ = (updateRateHz_ <= 0.0f) ? inst : (0.7f * updateRateHz_ + 0.3f * inst);
    }
  }
  prevAcceptMs_ = nowMs;

  ++receivedCount_;

  if (extracted < minObservedRaw_) {
    minObservedRaw_ = extracted;
  }
  if (extracted > maxObservedRaw_) {
    maxObservedRaw_ = extracted;
  }

  // Count out-of-clamp samples so the diagnostics expose an implausible scale.
  if (!isnan(settings_.offset) && !isinf(settings_.offset)) {
    const float est = static_cast<float>(extracted) * settings_.multiplier + settings_.offset;
    if (est < -0.5f || est > static_cast<float>(settings_.maxRpm) + 0.5f) {
      ++invalidSamples_;
    }
  }

  return true;
}

bool RpmCandidateMonitor::isFresh(uint32_t nowMs) const {
  if (!hasUpdate_) {
    return false;
  }
  return uint32_t(nowMs - lastUpdateMs_) < freshnessTimeoutMs_;
}

uint32_t RpmCandidateMonitor::lastUpdateAgeMs(uint32_t nowMs) const {
  if (!hasUpdate_) {
    return 0xFFFFFFFFUL;
  }
  return uint32_t(nowMs - lastUpdateMs_);
}

uint16_t RpmCandidateMonitor::estimatedRpm(uint32_t nowMs) const {
  if (!isFresh(nowMs) || (!hasUpdate_ && rawValue16_ == 0)) {
    return 0;
  }
  if (!validMultiplier(settings_.multiplier) || isnan(settings_.offset) || isinf(settings_.offset)) {
    return 0;
  }
  const float est = static_cast<float>(rawValue16_) * settings_.multiplier + settings_.offset + 0.5f;
  if (isnan(est) || est <= 0.0f) {
    return 0;
  }
  if (est > 65535.0f) {
    return 65535;
  }
  return static_cast<uint16_t>(est);
}

LiveRpmSnapshot RpmCandidateMonitor::snapshot(uint32_t nowMs) const {
  LiveRpmSnapshot s;
  s.multiplier = settings_.multiplier;
  s.offset = settings_.offset;
  s.byteIndex = settings_.byteIndex;
  s.byteWidth = settings_.byteWidth;
  s.littleEndian = settings_.littleEndian;
  s.canId = settings_.canId;
  s.extended = settings_.extended;
  s.calibrationVerified = calibrationVerified_;
  s.calibrationFingerprint = decoderFingerprint();
  // Honest value: the mirror is bound to the current decoder fingerprint by
  // syncDecoderToMonitor (re-binding requires an exact match), so the flag
  // already encodes "verified AND bound to these settings".
  s.calibrationMatchesSettings = calibrationVerified_;
  s.acceptedFrames = receivedCount_;
  s.rejectedFrames = rejectedCount_;
  s.invalidSamples = invalidSamples_;
  s.staleTransitions = staleTransitions_;

  s.available = hasUpdate_;
  s.fresh = isFresh(nowMs);
  s.frameAgeMs = hasUpdate_ ? uint32_t(nowMs - lastUpdateMs_) : 0xFFFFFFFEUL;
  s.lastUpdateMs = lastUpdateMs_;
  s.updateRateHz = updateRateHz_;
  s.raw = rawValue16_;
  s.dlc = lastFrameDlc_;
  memcpy(s.frameData, lastFrameData_, sizeof(s.frameData));

  if (s.fresh) {
    s.rpm = estimatedRpm(nowMs);
    s.valid = (rawValue16_ > 0) && s.rpm <= settings_.maxRpm && validMultiplier(settings_.multiplier);
    s.engineRunning = s.valid && s.rpm > 0;
  } else {
    s.rpm = 0;
    s.valid = false;
    s.engineRunning = false;
  }
  return s;
}

void RpmCandidateMonitor::reset() {
  rawValue16_ = 0;
  lastUpdateMs_ = 0;
  hasUpdate_ = false;
  receivedCount_ = 0;
  rejectedCount_ = 0;
  invalidSamples_ = 0;
  staleTransitions_ = 0;
  minObservedRaw_ = 0xFFFF;
  maxObservedRaw_ = 0;
  updateRateHz_ = 0.0f;
  prevAcceptMs_ = 0;
  memset(lastFrameData_, 0, sizeof(lastFrameData_));
  lastFrameDlc_ = 0;
}

void RpmCandidateMonitor::printStatus(uint32_t nowMs,
                                      bool controllerHealthy,
                                      uint32_t bitrate,
                                      bool busTrafficActive) const {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  const bool fresh = isFresh(nowMs);
  const uint32_t age = lastUpdateAgeMs(nowMs);

  RPM_PRINTLN("PULSESHIFT RPM MONITOR\r\n");
  RPM_PRINTF("CAN ID           : 0x%03lX\r\n", (unsigned long)settings_.canId);
  RPM_PRINTF("Byte Index       : %u (width %u, %s-endian)\r\n",
             (unsigned)settings_.byteIndex, (unsigned)settings_.byteWidth,
             settings_.littleEndian ? "little" : "big");

  if (!hasUpdate_) {
    RPM_PRINTLN("Raw Value        : -- (no frames received)");
    RPM_PRINTLN("Estimated RPM    : --");
  } else if (!fresh) {
    RPM_PRINTLN("Raw Value        : -- (signal stale)");
    RPM_PRINTLN("Estimated RPM    : INVALID (STALE)");
  } else {
    RPM_PRINTF("Raw Value        : %u\r\n", (unsigned)rawValue16_);
    RPM_PRINTF("Estimated RPM    : %u\r\n", (unsigned)estimatedRpm(nowMs));
  }

  RPM_PRINTF("Multiplier       : %.1f (offset %.1f, clamp %u)\r\n",
             settings_.multiplier, settings_.offset, (unsigned)settings_.maxRpm);
  RPM_PRINTF("Signal Fresh     : %s\r\n", fresh ? "YES" : (hasUpdate_ ? "NO (STALE)" : "NO (WAITING)"));

  if (hasUpdate_) {
    RPM_PRINTF("Last Update      : %lu ms ago | Rate: %.1f Hz\r\n",
               (unsigned long)age, (double)updateRateHz_);
  } else {
    RPM_PRINTLN("Last Update      : --");
  }

  RPM_PRINTF("Calibration      : %s\r\n", calibrationVerified_ ? "VERIFIED" : "UNVERIFIED");
  RPM_PRINTF("Verified LED Mode: %s\r\n", calibrationVerified_ ? "AVAILABLE" : "CALIBRATION REQUIRED");
  RPM_PRINTLN("CAN TX           : SOFTWARE MUTED (Verify CTX->3V3 & GPIO17 air-gap)");

  RPM_PRINTLN("\n--- Candidate Diagnostics ---");
  RPM_PRINTF("Frames Received  : %lu\r\n", (unsigned long)receivedCount_);
  RPM_PRINTF("Frames Rejected  : %lu\r\n", (unsigned long)rejectedCount_);
  RPM_PRINTF("Invalid Samples  : %lu | Stale Transitions: %lu\r\n",
             (unsigned long)invalidSamples_, (unsigned long)staleTransitions_);
  if (receivedCount_ > 0) {
    RPM_PRINTF("Observed Range   : Min: %u | Max: %u\r\n",
               (unsigned)minObservedRaw_, (unsigned)maxObservedRaw_);
  } else {
    RPM_PRINTLN("Observed Range   : --");
  }
  RPM_PRINTF("Freshness Window : %lu ms (~10-12 frames at 45 Hz)\r\n",
             (unsigned long)freshnessTimeoutMs_);

  RPM_PRINTLN("\n--- CAN Controller & Bus ---");
  RPM_PRINTF("Controller State : %s\r\n", controllerHealthy ? "HEALTHY (RUNNING)" : "FAULT (HALTED)");
  RPM_PRINTF("CAN Bitrate      : %lu bps\r\n", (unsigned long)bitrate);
  RPM_PRINTF("Bus Traffic      : %s\r\n", busTrafficActive ? "ACTIVE" : "WAITING / IDLE");
#else
  (void)nowMs; (void)controllerHealthy; (void)bitrate; (void)busTrafficActive;
#endif
}

void RpmCandidateMonitor::printStreamLine(uint32_t nowMs) const {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  if (!hasUpdate_) {
    RPM_PRINTF("[RPM] Waiting for CAN ID 0x%03lX... | Fresh: NO | Age: -- | 0x%03lX RX: 0\r\n",
               (unsigned long)settings_.canId, (unsigned long)settings_.canId);
    return;
  }

  const bool fresh = isFresh(nowMs);
  const uint32_t age = lastUpdateAgeMs(nowMs);

  if (fresh) {
    RPM_PRINTF("[RPM] Raw: %3u | Est: %5u RPM (x%.1f %s) | Age: %4lu ms | Fresh: YES | 0x%03lX RX: %lu\r\n",
               (unsigned)rawValue16_,
               (unsigned)estimatedRpm(nowMs),
               settings_.multiplier,
               calibrationVerified_ ? "VERIFIED" : "UNVERIFIED",
               (unsigned long)age,
               (unsigned long)settings_.canId,
               (unsigned long)receivedCount_);
  } else {
    RPM_PRINTF("[RPM] Raw:  -- | Est: ----- RPM (STALE)        | Age: %4lu ms | Fresh: NO  | 0x%03lX RX: %lu\r\n",
               (unsigned long)age,
               (unsigned long)settings_.canId,
               (unsigned long)receivedCount_);
  }
#else
  (void)nowMs;
#endif
}
