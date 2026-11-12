// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include "../UserConfig.h"
#ifndef PULSESHIFT_CONFIG_VALUES_READY
#define PULSESHIFT_CONFIG_VALUES_READY 1
#endif
#include "../ConfigCompat.h"
#include "shift_light.h"
#include "vehicle_profile.h"

#define PULSESHIFT_REPLAY 1
#define PULSESHIFT_TWAI_TEST 2
#define PULSESHIFT_BIKE 3 // Legacy capture target.
#define PULSESHIFT_CAPTURE 3
#define PULSESHIFT_VEHICLE 4
#include "../ConfigCompat.h"

#ifndef PULSESHIFT_MODE
#if PULSESHIFT_OPERATING_MODE == PULSESHIFT_USER_MODE_DEMO
#define PULSESHIFT_MODE PULSESHIFT_REPLAY
#elif PULSESHIFT_OPERATING_MODE == PULSESHIFT_USER_MODE_CAN_CAPTURE
#define PULSESHIFT_MODE PULSESHIFT_CAPTURE
#elif PULSESHIFT_OPERATING_MODE == PULSESHIFT_USER_MODE_VEHICLE_RPM
#define PULSESHIFT_MODE PULSESHIFT_VEHICLE
#else
#error "Unsupported PULSESHIFT_OPERATING_MODE"
#endif
#endif

#if PULSESHIFT_MODE < 1 || PULSESHIFT_MODE > 4
#error "Invalid PULSESHIFT_MODE"
#endif

#ifndef PULSESHIFT_DASHBOARD
#define PULSESHIFT_DASHBOARD 1
#endif

#ifndef PULSESHIFT_BITRATE
#define PULSESHIFT_BITRATE PULSESHIFT_CAN_BITRATE
#endif

#if PULSESHIFT_BITRATE != 0 && PULSESHIFT_BITRATE != 125000 && PULSESHIFT_BITRATE != 250000 && \
    PULSESHIFT_BITRATE != 500000 && PULSESHIFT_BITRATE != 1000000
#error "Unsupported PULSESHIFT_BITRATE"
#endif

#include "../ConfigCompat.h"

namespace config {
constexpr const char* vehicle = "Bajaj Pulsar NS125 2025 ABS";
constexpr const char* transceiver = "SN65HVD230";
// GPIO17 is disconnected from output in receive-only mode. Hold transceiver RS
// high in hardware and keep its TXD input recessive during isolated bench use.
constexpr int sda = PULSESHIFT_OLED_SDA_GPIO, scl = PULSESHIFT_OLED_SCL_GPIO;
constexpr int ledPin = PULSESHIFT_LED_DATA_GPIO, txPin = PULSESHIFT_CAN_TX_GPIO;
constexpr int rxPin = PULSESHIFT_CAN_RX_GPIO;
constexpr uint8_t ledCount = PULSESHIFT_LED_COUNT;
constexpr uint8_t ledBrightness = PULSESHIFT_LED_BRIGHTNESS;
constexpr uint16_t startRpm = PULSESHIFT_DEMO_START_RPM;
constexpr uint16_t shiftRpm = PULSESHIFT_DEMO_SHIFT_RPM;
constexpr uint16_t maxRpm = PULSESHIFT_DEMO_MAX_RPM;
constexpr uint16_t hysteresisRpm = PULSESHIFT_ALERT_HYSTERESIS_RPM;
constexpr uint16_t animationIntervalMs = PULSESHIFT_ANIMATION_INTERVAL_MS;
constexpr bool idleEffectEnabled = PULSESHIFT_IDLE_EFFECT_ENABLED != 0;
constexpr uint16_t idleMaxRpm = PULSESHIFT_IDLE_MAX_RPM;
constexpr ShiftLightStyle shiftLightStyle = static_cast<ShiftLightStyle>(PULSESHIFT_SHIFT_LIGHT_STYLE);
constexpr LedDirection ledDirection = static_cast<LedDirection>(PULSESHIFT_LED_DIRECTION);
constexpr bool startupAnimationEnabled = PULSESHIFT_STARTUP_ANIMATION_ENABLED != 0;
constexpr bool smoothTransitionsEnabled = PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED != 0;
constexpr ShiftAlertMode shiftAlertMode = static_cast<ShiftAlertMode>(PULSESHIFT_SHIFT_ALERT_MODE);

constexpr bool oledEnabled = PULSESHIFT_OLED_ENABLED != 0;
constexpr bool serialDiagnostics = PULSESHIFT_SERIAL_DIAGNOSTICS != 0;
constexpr uint32_t serialBaud = PULSESHIFT_SERIAL_BAUD;
constexpr uint32_t staleMs = 1000, txTimeoutMs = 1500, framePeriodMs = 100;
constexpr const char* modeName = PULSESHIFT_MODE == PULSESHIFT_REPLAY ? "DEMO_SIMULATED" :
  PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST ? "TWAI_SELF_TEST" :
  PULSESHIFT_MODE == PULSESHIFT_CAPTURE ? "CAN_CAPTURE" : "VEHICLE_RPM";

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
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
  const auto& p = selectedVehicleProfile();
  // Fail-closed placeholder bounds are never rendered as live RPM. No demo
  // threshold is used for the vehicle; reviewed profile calibration is required.
  const bool ready = vehicleProfileReady(p);
  c.rpmStart = ready ? p.lights.start : 0;
  c.rpmShift = ready ? p.lights.shift : 1;
  c.rpmMax = ready ? p.lights.maximum : 1;
  c.hysteresisRpm = ready ? p.lights.hysteresis : 0;
  c.idleEffectEnabled = false; // No vehicle idle-effect calibration supplied.
#endif
  constexpr RgbColor palette[] = {PULSESHIFT_CUSTOM_COLORS};
  static_assert(sizeof(palette)/sizeof(palette[0]) <= ShiftLightEngine::MAX_LEDS,
                "PULSESHIFT_CUSTOM_COLORS supports at most 32 entries");
  c.customColorCount = static_cast<uint8_t>(sizeof(palette)/sizeof(palette[0]));
  for (uint8_t i = 0; i < c.customColorCount; ++i) c.customColors[i] = palette[i];
  return c;
}
}
