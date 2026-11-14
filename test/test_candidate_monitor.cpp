// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "rpm_candidate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <limits>

namespace {
unsigned assertions = 0, cases = 0;
#define CHECK(expr) do { \
  ++assertions; \
  if (!(expr)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
    std::exit(1); \
  } \
} while(0)

void pass(const char* name) {
  ++cases;
  std::printf("PASS %s\n", name);
}

CanFrame make301Frame(uint8_t b0, uint8_t dlc = 8, bool ext = false, bool rtr = false) {
  CanFrame f{};
  f.id = 0x301;
  f.extended = ext;
  f.remote = rtr;
  f.dlc = dlc;
  f.data[0] = b0;
  f.data[7] = 0xC9; // Typical trailing byte in real Bajaj recording
  return f;
}

void test_default_initialization() {
  RpmCandidateMonitor mon;
  CHECK(mon.candidateId() == 0x301);
  CHECK(mon.byteIndex() == 0);
  CHECK(mon.multiplier() == 40.0f);
  CHECK(mon.freshnessTimeoutMs() == 250);
  CHECK(!mon.hasReceivedFrame());
  CHECK(mon.receivedCount() == 0);
  CHECK(mon.rejectedCount() == 0);
  CHECK(!mon.isFresh(100));
  CHECK(mon.estimatedRpm(100) == 0);
  CHECK(!mon.isCalibrated());
  CHECK(!mon.isShiftLightEnabled());
  CHECK(mon.isTxIsolated());
  pass("default candidate monitor initialization and safety flags");
}

void test_valid_frame_decoding() {
  RpmCandidateMonitor mon;

  // 1. Idle candidate frame (raw 37 -> 1480 RPM)
  CanFrame f = make301Frame(37);
  CHECK(mon.ingest(f, 1000));
  CHECK(mon.hasReceivedFrame());
  CHECK(mon.receivedCount() == 1);
  CHECK(mon.rejectedCount() == 0);
  CHECK(mon.rawValue() == 37);
  CHECK(mon.isFresh(1020));
  CHECK(mon.lastUpdateAgeMs(1020) == 20);
  CHECK(mon.estimatedRpm(1020) == 1480);

  // 2. Engine off frame (raw 0 -> 0 RPM)
  f = make301Frame(0);
  CHECK(mon.ingest(f, 1040));
  CHECK(mon.rawValue() == 0);
  CHECK(mon.isFresh(1050));
  CHECK(mon.estimatedRpm(1050) == 0);

  // 3. Peak sweep frame (raw 240 -> 9600 RPM)
  f = make301Frame(240);
  CHECK(mon.ingest(f, 1060));
  CHECK(mon.rawValue() == 240);
  CHECK(mon.isFresh(1070));
  CHECK(mon.estimatedRpm(1070) == 9600);

  // Check min and max tracking
  CHECK(mon.minObservedRaw() == 0);
  CHECK(mon.maxObservedRaw() == 240);
  pass("valid 0x301 frames decoding, scaling, and min/max tracking");
}

void test_invalid_envelopes_rejected() {
  RpmCandidateMonitor mon;

  // Frame with different ID should be ignored (not counted as rejected candidate)
  CanFrame other{};
  other.id = 0x302;
  other.dlc = 8;
  CHECK(!mon.ingest(other, 100));
  CHECK(mon.receivedCount() == 0);
  CHECK(mon.rejectedCount() == 0);

  // 0x301 with extended flag set -> rejected
  CanFrame ext = make301Frame(37, 8, true, false);
  CHECK(!mon.ingest(ext, 200));
  CHECK(mon.rejectedCount() == 1);

  // 0x301 with remote RTR flag set -> rejected
  CanFrame rtr = make301Frame(37, 8, false, true);
  CHECK(!mon.ingest(rtr, 300));
  CHECK(mon.rejectedCount() == 2);

  // 0x301 with incorrect DLC -> rejected
  CanFrame badDlc = make301Frame(37, 7, false, false);
  CHECK(!mon.ingest(badDlc, 400));
  CHECK(mon.rejectedCount() == 3);

  CanFrame zeroDlc = make301Frame(37, 0, false, false);
  CHECK(!mon.ingest(zeroDlc, 500));
  CHECK(mon.rejectedCount() == 4);

  pass("rejection of extended, remote, and wrong DLC candidate frames");
}

void test_freshness_timeout_and_recovery() {
  RpmCandidateMonitor mon;

  CanFrame f = make301Frame(37);
  CHECK(mon.ingest(f, 1000));
  CHECK(mon.isFresh(1000));
  CHECK(mon.isFresh(1249)); // 249 ms elapsed: fresh
  CHECK(mon.estimatedRpm(1249) == 1480);

  // Exactly at or beyond 250 ms timeout: becomes stale
  CHECK(!mon.isFresh(1250));
  CHECK(!mon.isFresh(1500));
  CHECK(mon.estimatedRpm(1250) == 0); // Stale frames return 0 / marked invalid
  CHECK(mon.lastUpdateAgeMs(1500) == 500);

  // New frame revives freshness
  f = make301Frame(38);
  CHECK(mon.ingest(f, 2000));
  CHECK(mon.isFresh(2005));
  CHECK(mon.estimatedRpm(2005) == 1520);
  pass("freshness expiration window and automatic recovery on new frame");
}

void test_timer_rollover() {
  RpmCandidateMonitor mon;

  // Simulate arrival just before 32-bit millis() overflow (0xFFFFFFFF)
  CanFrame f = make301Frame(50);
  CHECK(mon.ingest(f, 0xFFFFFFF0UL));

  // 10 ms later, millis() wraps past zero to 0x00000004
  const uint32_t postWrap = 4;
  CHECK(mon.isFresh(postWrap));
  CHECK(mon.lastUpdateAgeMs(postWrap) == 20); // (4 - 0xFFFFFFF0) == 20 in 32-bit unsigned math
  CHECK(mon.estimatedRpm(postWrap) == 2000);

  // 300 ms after arrival (wrapped to 284): should be stale
  const uint32_t staleTime = 284;
  CHECK(!mon.isFresh(staleTime));
  CHECK(mon.lastUpdateAgeMs(staleTime) == 300);
  pass("timer rollover handling across 32-bit boundary");
}

void test_custom_multiplier() {
  RpmCandidateMonitor mon(0x301, 0, 42.4242f);
  CanFrame f = make301Frame(33);
  CHECK(mon.ingest(f, 100));
  // 33 * 42.4242 = 1400.0
  CHECK(mon.estimatedRpm(100) == 1400);

  mon.setMultiplier(50.0f);
  CHECK(mon.estimatedRpm(100) == 1650);
  pass("configurable provisional multiplier calculation");
}

void test_status_and_stream_formatting() {
  RpmCandidateMonitor mon;
  // Test printing without crashing when empty
  mon.printStatus(100, true, 500000, false);
  mon.printStreamLine(100);

  // Ingest frame
  CanFrame f = make301Frame(37);
  mon.ingest(f, 100);
  mon.printStatus(120, true, 500000, true);
  mon.printStreamLine(120);

  // Stale
  mon.printStatus(500, true, 500000, true);
  mon.printStreamLine(500);

  pass("status and stream line formatting functions");
}

void test_bounds_and_nan_safety() {
  // Test invalid byte index clamping in constructor
  RpmCandidateMonitor badIdx(0x301, 10, 40.0f);
  CHECK(badIdx.byteIndex() == 0); // clamped to 0

  badIdx.setByteIndex(15);
  CHECK(badIdx.byteIndex() == 0); // ignored, stays 0

  badIdx.setByteIndex(4);
  CHECK(badIdx.byteIndex() == 4);

  // Test NaN / Inf multiplier rejection
  float nanVal = std::numeric_limits<float>::quiet_NaN();
  float infVal = std::numeric_limits<float>::infinity();
  badIdx.setMultiplier(nanVal);
  CHECK(badIdx.multiplier() == 40.0f); // unchanged

  badIdx.setMultiplier(infVal);
  CHECK(badIdx.multiplier() == 40.0f); // unchanged

  badIdx.setMultiplier(-10.0f);
  CHECK(badIdx.multiplier() == 40.0f); // unchanged

  badIdx.setMultiplier(2000.0f);
  CHECK(badIdx.multiplier() == 40.0f); // exceeds 1000 limit, unchanged

  badIdx.setMultiplier(60.0f);
  CHECK(badIdx.multiplier() == 60.0f);

  // Test DLC bounds check in ingest
  RpmCandidateMonitor mon4(0x301, 4, 40.0f);
  CanFrame f = make301Frame(37, 8);
  f.data[4] = 42;
  CHECK(mon4.ingest(f, 100));
  CHECK(mon4.rawValue() == 42);

  // If byteIndex is 4, but dlc is 3, frame must be rejected
  CanFrame shortDlc = make301Frame(37, 3);
  CHECK(!mon4.ingest(shortDlc, 200));

  pass("bounds checking, NaN/Inf rejection, and multiplier bounds safety");
}

void test_extended_can_id_support() {
  // 29-bit candidate ID
  RpmCandidateMonitor extMon(0x18FEE000, 0, 0.125f);
  CHECK(extMon.candidateId() == 0x18FEE000);

  // Standard frame for extended candidate must be rejected
  CanFrame stdFrame{};
  stdFrame.id = 0x18FEE000;
  stdFrame.extended = false;
  stdFrame.dlc = 8;
  CHECK(!extMon.ingest(stdFrame, 100));
  CHECK(extMon.rejectedCount() == 1);

  // Extended frame must be accepted
  CanFrame extFrame{};
  extFrame.id = 0x18FEE000;
  extFrame.extended = true;
  extFrame.dlc = 8;
  extFrame.data[0] = 80;
  CHECK(extMon.ingest(extFrame, 200));
  CHECK(extMon.receivedCount() == 1);
  CHECK(extMon.rawValue() == 80);

  pass("extended 29-bit CAN ID candidate frame support");
}

void test_stale_dropout_counting() {
  RpmCandidateMonitor mon;
  CanFrame f = make301Frame(37);

  CHECK(mon.ingest(f, 1000));
  auto snap = mon.snapshot(1100);
  CHECK(snap.staleTransitions == 0); // continuous fresh signal

  // A frame arriving after the freshness window expired marks one dropout.
  CHECK(mon.ingest(f, 1500));
  snap = mon.snapshot(1510);
  CHECK(snap.staleTransitions == 1);

  // Rapid frames add no further dropouts.
  CHECK(mon.ingest(f, 1520));
  CHECK(mon.ingest(f, 1540));
  snap = mon.snapshot(1550);
  CHECK(snap.staleTransitions == 1);

  // A second gap counts again.
  CHECK(mon.ingest(f, 3000));
  snap = mon.snapshot(3010);
  CHECK(snap.staleTransitions == 2);
  pass("staleTransitions counts real signal dropouts");
}

} // namespace

int main() {
  test_default_initialization();
  test_valid_frame_decoding();
  test_invalid_envelopes_rejected();
  test_freshness_timeout_and_recovery();
  test_timer_rollover();
  test_custom_multiplier();
  test_bounds_and_nan_safety();
  test_extended_can_id_support();
  test_status_and_stream_formatting();
  test_stale_dropout_counting();

  std::printf("ALL PASS: %u cases, %u assertions\n", cases, assertions);
  return 0;
}
