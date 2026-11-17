// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "led_driver.h"

#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { \
  std::fprintf(stderr, "LED_RMT FAIL %d: %s\n", __LINE__, #x); std::exit(1); \
} } while (0)

namespace {
struct FakeRmt {
  bool attached = false;
  bool eotSet = false;
  bool rejectNext = false;
  bool failEot = false;
  bool failDetach = false;
  bool active = false;
  bool doneFlag = true;
  unsigned inits = 0;
  unsigned submits = 0;
  unsigned detaches = 0;
  uint32_t now = 0;
  uint32_t submitTime = 0;
  uint32_t completeAfterMs = 1;
  const rmt_data_t* data = nullptr;
  size_t length = 0;
};
FakeRmt hal;
}

uint32_t millis() { return hal.now; }
void delay(uint32_t ms) { hal.now += ms; }

bool rmtInit(int pin, rmt_ch_dir_t direction, rmt_reserve_memsize_t blocks, uint32_t hz) {
  CHECK(pin == 27);
  CHECK(direction == RMT_TX_MODE);
  CHECK(blocks == RMT_MEM_NUM_BLOCKS_4);
  CHECK(hz == 10000000);
  CHECK(!hal.attached);
  ++hal.inits;
  hal.attached = true;
  hal.doneFlag = true;
  return true;
}

bool rmtSetEOT(int pin, uint8_t level) {
  CHECK(pin == 27);
  CHECK(hal.attached);
  CHECK(level == 0);
  hal.eotSet = !hal.failEot;
  return hal.eotSet;
}

bool rmtWriteAsync(int pin, rmt_data_t* data, size_t symbols) {
  CHECK(pin == 27);
  CHECK(hal.attached && hal.eotSet);
  CHECK(hal.doneFlag);
  ++hal.submits;
  hal.doneFlag = false;
  if (hal.rejectNext) {
    hal.rejectNext = false;
    return false; // 3.3.12 leaves TX_DONE false when IDF rejects the submission.
  }
  hal.active = true;
  hal.submitTime = hal.now;
  hal.data = data;
  hal.length = symbols;
  return true;
}

bool rmtTransmitCompleted(int pin) {
  CHECK(pin == 27);
  CHECK(hal.attached);
  if (hal.active && uint32_t(hal.now - hal.submitTime) >= hal.completeAfterMs) {
    hal.active = false;
    hal.doneFlag = true;
  }
  return hal.doneFlag;
}

bool rmtDeinit(int pin) {
  CHECK(pin == 27);
  CHECK(hal.attached);
  CHECK(!hal.active);
  ++hal.detaches;
  if (hal.failDetach) return false;
  hal.attached = false;
  hal.eotSet = false;
  return true;
}

static void test_grb_with_latch() {
  hal = {};
  Esp32RmtLedDriver driver;
  CHECK(driver.begin(27, 6));
  RgbColor pixels[6]{{0xA1, 0x82, 0x03}, {0, 0, 0}, {0, 0, 0},
                     {0, 0, 0}, {0, 0, 0}, {0x7e, 0x23, 0x5d}};
  CHECK(!driver.write(pixels, 7));
  CHECK(driver.write(pixels, 6));
  CHECK(hal.length == 145);
  CHECK(hal.length + 1 <= 4 * 64); // The HAL/IDF appends one EOF symbol.
  const uint8_t expectedGrb[]{0x82, 0xA1, 0x03, 0, 0, 0, 0, 0, 0,
                              0, 0, 0, 0, 0, 0, 0x23, 0x7e, 0x5d};
  for (size_t i = 0; i < sizeof(expectedGrb); ++i) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const bool one = (expectedGrb[i] >> (7 - bit)) & 1;
      const auto symbol = hal.data[i * 8 + bit];
      CHECK(symbol.level0 == 1 && symbol.level1 == 0);
      CHECK(symbol.duration0 == (one ? 8u : 3u));
      CHECK(symbol.duration1 == (one ? 6u : 9u));
    }
  }
  const auto latch = hal.data[144];
  CHECK(latch.level0 == 0 && latch.level1 == 0);
  CHECK(latch.duration0 + latch.duration1 == 3000);
}

static void test_rejected_submission_and_partial_setup_recover() {
  hal = {};
  Esp32RmtLedDriver driver;
  hal.failEot = true;
  CHECK(!driver.begin(27, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::InitFailed);
  CHECK(!hal.attached);
  hal.failEot = false;
  CHECK(driver.begin(27, 6));

  RgbColor pixels[6]{};
  hal.rejectNext = true;
  CHECK(!driver.write(pixels, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::TransmitRejected);
  CHECK(!driver.isReady() && !hal.attached);
  CHECK(driver.begin(27, 6));
  CHECK(driver.write(pixels, 6));
  CHECK(hal.inits == 3);
  CHECK(hal.detaches == 2);
}

static void test_timeout_does_not_reuse_pending_payload() {
  hal = {};
  Esp32RmtLedDriver driver;
  CHECK(driver.begin(27, 6));
  hal.now = UINT32_MAX - 10;
  hal.completeAfterMs = 40;
  RgbColor lit[6]{{0, 0xff, 0}};
  RgbColor clear[6]{};
  CHECK(!driver.write(lit, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::Timeout);
  CHECK(driver.isReady() && hal.active);
  CHECK(uint32_t(hal.now - hal.submitTime) == 20);
  CHECK(hal.data[0].duration0 == 8);
  CHECK(!driver.write(clear, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::Busy);
  CHECK(hal.data[0].duration0 == 8);
  CHECK(hal.submits == 1 && hal.detaches == 0);

  hal.now += 20;
  CHECK(rmtTransmitCompleted(27));
  hal.completeAfterMs = 1;
  CHECK(driver.write(clear, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::TransmitOk);
  CHECK(hal.data[0].duration0 == 3);
}

static void test_failed_detach_does_not_touch_stale_mapping() {
  hal = {};
  Esp32RmtLedDriver driver;
  CHECK(driver.begin(27, 6));
  hal.rejectNext = true;
  hal.failDetach = true;
  RgbColor clear[6]{};
  CHECK(!driver.write(clear, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::DeinitFailed);
  CHECK(!driver.begin(27, 6));
  CHECK(hal.inits == 1 && hal.detaches == 1);
}

static void test_permanent_stall_stops_output_without_releasing_payload() {
  hal = {};
  Esp32RmtLedDriver driver;
  CHECK(driver.begin(27, 6));
  hal.now = UINT32_MAX - 10;
  hal.completeAfterMs = UINT32_MAX;
  RgbColor lit[6]{{0, 0xff, 0}};
  RgbColor clear[6]{};
  CHECK(!driver.write(lit, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::Timeout);

  hal.now = hal.submitTime + 99;
  CHECK(!driver.write(clear, 6));
  CHECK(driver.isReady());
  CHECK(driver.lastStatus() == LedDriverStatus::Busy);
  hal.now += 1;
  CHECK(!driver.write(clear, 6));
  CHECK(!driver.isReady());
  CHECK(driver.isTerminalFault());
  CHECK(driver.lastStatus() == LedDriverStatus::TransmitStalled);

  hal.now += 1000;
  CHECK(!driver.begin(27, 6));
  CHECK(!driver.write(clear, 6));
  CHECK(driver.lastStatus() == LedDriverStatus::TransmitStalled);
  CHECK(hal.active && hal.inits == 1 && hal.submits == 1 && hal.detaches == 0);
  CHECK(hal.data[0].duration0 == 8);
}

int main() {
  test_grb_with_latch();
  test_rejected_submission_and_partial_setup_recover();
  test_timeout_does_not_reuse_pending_payload();
  test_failed_detach_does_not_touch_stale_mapping();
  test_permanent_stall_stops_output_without_releasing_payload();
  std::printf("PASS ESP32 RMT driver timing, rejection recovery and pending payload lifetime: %u checks\n", checks);
}
