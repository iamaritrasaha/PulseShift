#pragma once
#include <stdint.h>
#define REVIX_REPLAY 1
#define REVIX_TWAI_TEST 2
#define REVIX_BIKE 3
#ifndef REVIX_MODE
// Wokwi mode: REVIX_TWAI_TEST tests the controller; REVIX_REPLAY tests frames/UI.
// Change this line, build, then restart Wokwi. Bike builds override it safely.
#define REVIX_MODE REVIX_REPLAY
#endif
#if REVIX_MODE < 1 || REVIX_MODE > 3
#error "Invalid REVIX_MODE"
#endif
#ifndef REVIX_DASHBOARD
#define REVIX_DASHBOARD 1
#endif
#ifndef REVIX_BITRATE
#if REVIX_MODE == REVIX_BIKE
#define REVIX_BITRATE 0 // NS125 2025 ABS bitrate is not verified.
#else
#define REVIX_BITRATE 500000
#endif
#endif
namespace config {
constexpr const char* vehicle = "Bajaj Pulsar NS125 2025 ABS";
constexpr const char* transceiver = "SN65HVD230";
// ESP32 DevKit V1; SN65HVD230 R/RXD -> RX16, D/TXD -> TX17.
// Bike capture disables TX17 output. Hold the transceiver RS high in hardware.
constexpr int sda = 21, scl = 22, ledPin = 27, txPin = 17, rxPin = 16;
constexpr uint8_t ledCount = 6;
constexpr uint8_t ledBrightness = 40; // 0..255; not an electrical current limit.
constexpr uint16_t maxRpm = 6500; // Demonstration scale, NOT the NS125 redline.
constexpr uint32_t staleMs = 1000, txTimeoutMs = 1500, framePeriodMs = 100;
constexpr const char* modeName = REVIX_MODE == REVIX_REPLAY ? "FRAME_REPLAY_TEST" :
  REVIX_MODE == REVIX_TWAI_TEST ? "TWAI_SELF_TEST" : "BIKE_LISTEN_ONLY";
}
