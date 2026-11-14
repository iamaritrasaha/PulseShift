// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#if __has_include("../UserConfig.h")
#include "../UserConfig.h"
#elif __has_include("UserConfig.h")
#include "UserConfig.h"
#endif

// ============================================================================
// Single production operating path.
//
// There is no compile-time mode matrix anymore: the firmware always runs the
// same standalone pipeline (passive CAN monitoring + unified RPM decoder +
// unified LED controller + BLE configuration/telemetry + flash capture as a
// built-in subsystem). Capture recording is a runtime toggle (BOOT button,
// serial command or BLE command), never a recompile.
// ============================================================================

#ifndef PULSESHIFT_BITRATE
#define PULSESHIFT_BITRATE PULSESHIFT_CAN_BITRATE
#endif

#if PULSESHIFT_BITRATE != 0 && PULSESHIFT_BITRATE != 125000 && PULSESHIFT_BITRATE != 250000 && \
    PULSESHIFT_BITRATE != 500000 && PULSESHIFT_BITRATE != 1000000
#error "Unsupported PULSESHIFT_BITRATE"
#endif

namespace config {
constexpr const char* vehicle = "Bajaj Pulsar NS125 2025 ABS";
constexpr const char* transceiver = "SN65HVD230";
// GPIO17 is disconnected from output in receive-only mode. SN65HVD230 pin 8 (Rs)
// connects to GND for normal high-speed operation; pin 1 (TXD) is held recessive (3.3V)
// and GPIO17 remains physically disconnected so no transmission can occur.
constexpr const char* modeName = "PRODUCTION";
constexpr int ledPin = PULSESHIFT_LED_DATA_GPIO, txPin = PULSESHIFT_CAN_TX_GPIO;
constexpr int rxPin = PULSESHIFT_CAN_RX_GPIO;
constexpr uint8_t ledCount = PULSESHIFT_LED_COUNT;
constexpr bool serialDiagnostics = PULSESHIFT_SERIAL_DIAGNOSTICS != 0;
constexpr bool bleEnabled = PULSESHIFT_BLE_ENABLED != 0;
constexpr uint32_t serialBaud = PULSESHIFT_SERIAL_BAUD;
constexpr uint32_t bitrate = PULSESHIFT_BITRATE;
}
