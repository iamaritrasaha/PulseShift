// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include "../UserConfig.h"
#include "shift_light.h"

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
constexpr uint16_t startRpm = REVIX_DEMO_START_RPM;
constexpr uint16_t shiftRpm = REVIX_DEMO_SHIFT_RPM;
constexpr uint16_t maxRpm = REVIX_DEMO_MAX_RPM;
constexpr uint16_t hysteresisRpm = REVIX_ALERT_HYSTERESIS_RPM;
constexpr uint16_t animationIntervalMs = REVIX_ANIMATION_INTERVAL_MS;
constexpr bool idleEffectEnabled = REVIX_IDLE_EFFECT_ENABLED != 0;
constexpr uint16_t idleMaxRpm = REVIX_IDLE_MAX_RPM;
constexpr ShiftLightStyle shiftLightStyle = static_cast<ShiftLightStyle>(REVIX_SHIFT_LIGHT_STYLE);
constexpr LedDirection ledDirection = static_cast<LedDirection>(REVIX_LED_DIRECTION);
constexpr bool startupAnimationEnabled = REVIX_STARTUP_ANIMATION_ENABLED != 0;
constexpr bool smoothTransitionsEnabled = REVIX_SMOOTH_TRANSITIONS_ENABLED != 0;
constexpr ShiftAlertMode shiftAlertMode = static_cast<ShiftAlertMode>(REVIX_SHIFT_ALERT_MODE);

constexpr bool oledEnabled = REVIX_OLED_ENABLED != 0;
constexpr bool serialDiagnostics = REVIX_SERIAL_DIAGNOSTICS != 0;
constexpr uint32_t serialBaud = REVIX_SERIAL_BAUD;
constexpr uint32_t staleMs = 1000, txTimeoutMs = 1500, framePeriodMs = 100;
constexpr const char* modeName = REVIX_MODE == REVIX_REPLAY ? "DEMO_SIMULATED" :
  REVIX_MODE == REVIX_TWAI_TEST ? "TWAI_SELF_TEST" : "BENCH_LISTEN_ONLY";

inline ShiftLightConfig makeShiftLightConfig() {
  ShiftLightConfig c;
  c.ledCount = ledCount;
  c.masterBrightness = ledBrightness;
  c.style = shiftLightStyle;
  c.direction = ledDirection;
  c.startupAnimationEnabled = startupAnimationEnabled;
  c.smoothTransitions = smoothTransitionsEnabled;
  c.alertMode = shiftAlertMode;
  c.rpmStart = startRpm;
  c.rpmShift = shiftRpm;
  c.rpmMax = maxRpm;
  c.hysteresisRpm = hysteresisRpm;
  c.animationIntervalMs = animationIntervalMs;
  c.idleEffectEnabled = idleEffectEnabled;
  c.idleMaxRpm = idleMaxRpm;
  constexpr RgbColor palette[] = {REVIX_CUSTOM_COLORS};
  static_assert(sizeof(palette)/sizeof(palette[0]) <= ShiftLightEngine::MAX_LEDS,
                "REVIX_CUSTOM_COLORS supports at most 32 entries");
  c.customColorCount = static_cast<uint8_t>(sizeof(palette)/sizeof(palette[0]));
  for (uint8_t i = 0; i < c.customColorCount; ++i) c.customColors[i] = palette[i];
  return c;
}
}
