// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "rpm_candidate.h"
#include <stdio.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #define RPM_PRINTF(...) Serial.printf(__VA_ARGS__)
  #define RPM_PRINTLN(x) Serial.println(x)
#elif defined(__has_include)
  #if __has_include(<Arduino.h>)
    #include <Arduino.h>
    #define RPM_PRINTF(...) Serial.printf(__VA_ARGS__)
    #define RPM_PRINTLN(x) Serial.println(x)
  #else
    #define RPM_PRINTF(...) printf(__VA_ARGS__)
    #define RPM_PRINTLN(x) printf("%s\r\n", (x))
  #endif
#else
  #define RPM_PRINTF(...) printf(__VA_ARGS__)
  #define RPM_PRINTLN(x) printf("%s\r\n", (x))
#endif

RpmCandidateMonitor::RpmCandidateMonitor(uint32_t candidateId,
                                         uint8_t byteIndex,
                                         float multiplier,
                                         uint32_t freshnessTimeoutMs)
    : candidateId_(candidateId),
      byteIndex_(byteIndex),
      multiplier_(multiplier),
      freshnessTimeoutMs_(freshnessTimeoutMs) {}

bool RpmCandidateMonitor::ingest(const CanFrame& frame, uint32_t nowMs) {
  if (frame.id != candidateId_) {
    return false;
  }

  // Reject invalid candidate frame envelopes:
  // Must be standard 11-bit identifier, not RTR, and DLC must be exactly 8 bytes
  // matching the Bajaj Pulsar NS125 broadcast specification.
  if (frame.extended || frame.remote || frame.dlc != 8 || !validEnvelope(frame)) {
    ++rejectedCount_;
    return false;
  }

  rawValue_ = frame.data[byteIndex_];
  lastUpdateMs_ = nowMs;
  ++receivedCount_;

  if (rawValue_ < minObservedRaw_) {
    minObservedRaw_ = rawValue_;
  }
  if (rawValue_ > maxObservedRaw_) {
    maxObservedRaw_ = rawValue_;
  }

  return true;
}

bool RpmCandidateMonitor::isFresh(uint32_t nowMs) const {
  if (receivedCount_ == 0) {
    return false;
  }
  return uint32_t(nowMs - lastUpdateMs_) < freshnessTimeoutMs_;
}

uint32_t RpmCandidateMonitor::lastUpdateAgeMs(uint32_t nowMs) const {
  if (receivedCount_ == 0) {
    return 0xFFFFFFFFUL;
  }
  return uint32_t(nowMs - lastUpdateMs_);
}

uint16_t RpmCandidateMonitor::estimatedRpm(uint32_t nowMs) const {
  if (!isFresh(nowMs) || rawValue_ == 0) {
    return 0;
  }
  const float est = static_cast<float>(rawValue_) * multiplier_ + 0.5f;
  if (est > 65535.0f) {
    return 65535;
  }
  return static_cast<uint16_t>(est);
}

void RpmCandidateMonitor::reset() {
  rawValue_ = 0;
  lastUpdateMs_ = 0;
  receivedCount_ = 0;
  rejectedCount_ = 0;
  minObservedRaw_ = 0xFF;
  maxObservedRaw_ = 0;
}

void RpmCandidateMonitor::printStatus(uint32_t nowMs,
                                      bool controllerHealthy,
                                      uint32_t bitrate,
                                      bool busTrafficActive) const {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  const bool fresh = isFresh(nowMs);
  const uint32_t age = lastUpdateAgeMs(nowMs);

  RPM_PRINTLN("PULSESHIFT RPM MONITOR\r\n");
  RPM_PRINTF("CAN ID           : 0x%03lX\r\n", (unsigned long)candidateId_);
  RPM_PRINTF("Byte Index       : %u\r\n", (unsigned)byteIndex_);

  if (receivedCount_ == 0) {
    RPM_PRINTLN("Raw Value        : -- (no frames received)");
    RPM_PRINTLN("Estimated RPM    : --");
  } else if (!fresh) {
    RPM_PRINTLN("Raw Value        : -- (signal stale)");
    RPM_PRINTLN("Estimated RPM    : INVALID (STALE)");
  } else {
    RPM_PRINTF("Raw Value        : %u\r\n", (unsigned)rawValue_);
    RPM_PRINTF("Estimated RPM    : %u\r\n", (unsigned)estimatedRpm(nowMs));
  }

  RPM_PRINTF("Multiplier       : %.1f\r\n", multiplier_);
  RPM_PRINTF("Signal Fresh     : %s\r\n", fresh ? "YES" : (receivedCount_ == 0 ? "NO (WAITING)" : "NO (STALE)"));

  if (receivedCount_ == 0) {
    RPM_PRINTLN("Last Update      : --");
  } else {
    RPM_PRINTF("Last Update      : %lu ms ago\r\n", (unsigned long)age);
  }

  RPM_PRINTLN("Calibration      : UNVERIFIED");
  RPM_PRINTLN("LED Shift Mode   : DISABLED");
  RPM_PRINTLN("CAN TX           : PHYSICALLY ISOLATED");

  RPM_PRINTLN("\n--- Candidate Diagnostics ---");
  RPM_PRINTF("Frames Received  : %lu\r\n", (unsigned long)receivedCount_);
  RPM_PRINTF("Frames Rejected  : %lu\r\n", (unsigned long)rejectedCount_);
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
  if (receivedCount_ == 0) {
    RPM_PRINTF("[RPM] Waiting for CAN ID 0x%03lX... | Fresh: NO | Age: -- | 0x%03lX RX: 0\r\n",
               (unsigned long)candidateId_, (unsigned long)candidateId_);
    return;
  }

  const bool fresh = isFresh(nowMs);
  const uint32_t age = lastUpdateAgeMs(nowMs);

  if (fresh) {
    RPM_PRINTF("[RPM] Raw: %3u | Est: %5u RPM (x%.1f UNVERIFIED) | Age: %4lu ms | Fresh: YES | 0x%03lX RX: %lu\r\n",
               (unsigned)rawValue_,
               (unsigned)estimatedRpm(nowMs),
               multiplier_,
               (unsigned long)age,
               (unsigned long)candidateId_,
               (unsigned long)receivedCount_);
  } else {
    RPM_PRINTF("[RPM] Raw:  -- | Est: ----- RPM (STALE)        | Age: %4lu ms | Fresh: NO  | 0x%03lX RX: %lu\r\n",
               (unsigned long)age,
               (unsigned long)candidateId_,
               (unsigned long)receivedCount_);
  }
#else
  (void)nowMs;
#endif
}
