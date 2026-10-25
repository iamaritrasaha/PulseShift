// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include "../UserConfig.h"
#define REVIX_REPLAY 1
#define REVIX_TWAI_TEST 2
#define REVIX_BIKE 3
#ifndef REVIX_MODE
#if REVIX_OPERATING_MODE == REVIX_USER_MODE_DEMO
#define REVIX_MODE REVIX_REPLAY
#elif REVIX_OPERATING_MODE == REVIX_USER_MODE_BENCH_LISTEN_ONLY
#define REVIX_MODE REVIX_BIKE
#else
#error "Unsupported REVIX_OPERATING_MODE"
#endif
#endif
#if REVIX_MODE < 1 || REVIX_MODE > 3
#error "Invalid REVIX_MODE"
#endif
#ifndef REVIX_DASHBOARD
#define REVIX_DASHBOARD 1
#endif
#ifndef REVIX_BITRATE
#define REVIX_BITRATE REVIX_CAN_BITRATE
#endif
#if REVIX_BITRATE != 0 && REVIX_BITRATE != 125000 && REVIX_BITRATE != 250000 && \
    REVIX_BITRATE != 500000 && REVIX_BITRATE != 1000000
#error "Unsupported REVIX_BITRATE"
#endif
namespace config {
constexpr const char* vehicle = "Bajaj Pulsar NS125 2025 ABS";
constexpr const char* transceiver = "SN65HVD230";
// GPIO17 is disconnected from output in receive-only mode. Hold transceiver RS
// high in hardware and keep its TXD input recessive during isolated bench use.
constexpr int sda = REVIX_OLED_SDA_GPIO, scl = REVIX_OLED_SCL_GPIO;
constexpr int ledPin = REVIX_LED_DATA_GPIO, txPin = REVIX_CAN_TX_GPIO;
constexpr int rxPin = REVIX_CAN_RX_GPIO;
constexpr uint8_t ledCount = REVIX_LED_COUNT;
constexpr uint8_t ledBrightness = REVIX_LED_BRIGHTNESS;
constexpr uint16_t maxRpm = REVIX_DEMO_MAX_RPM;
constexpr bool oledEnabled = REVIX_OLED_ENABLED != 0;
constexpr bool serialDiagnostics = REVIX_SERIAL_DIAGNOSTICS != 0;
constexpr uint32_t serialBaud = REVIX_SERIAL_BAUD;
constexpr uint32_t staleMs = 1000, txTimeoutMs = 1500, framePeriodMs = 100;
constexpr const char* modeName = REVIX_MODE == REVIX_REPLAY ? "DEMO_SIMULATED" :
  REVIX_MODE == REVIX_TWAI_TEST ? "TWAI_SELF_TEST" : "BENCH_LISTEN_ONLY";
}
