// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// User settings for the ESP32 DevKit V1.
// Demo mode is safe to run without a motorcycle or CAN connection.
#define REVIX_USER_MODE_DEMO 1
#define REVIX_USER_MODE_BENCH_LISTEN_ONLY 3
#define REVIX_OPERATING_MODE REVIX_USER_MODE_DEMO

// Six WS2812B LEDs on the default REVIX LINK harness.
#define REVIX_LED_DATA_GPIO 27
#define REVIX_LED_COUNT 6
#define REVIX_LED_BRIGHTNESS 40  // 0..255; not an electrical current limit.

// Optional development display. Leave disabled for the LED-only device.
#ifndef REVIX_OLED_ENABLED
#define REVIX_OLED_ENABLED 0
#endif
#define REVIX_OLED_SDA_GPIO 21
#define REVIX_OLED_SCL_GPIO 22

// Isolated bench CAN capture only. 0 keeps the TWAI controller stopped.
// Select a nonzero rate only after verifying it on an isolated bench.
#define REVIX_CAN_RX_GPIO 16
#define REVIX_CAN_TX_GPIO 17
#define REVIX_CAN_BITRATE 0  // Supported: 0, 125000, 250000, 500000, 1000000.

// Demonstration scale only. This is not a motorcycle RPM calibration.
#define REVIX_DEMO_MAX_RPM 6500

// Serial Monitor diagnostics.
#define REVIX_SERIAL_DIAGNOSTICS 1
#define REVIX_SERIAL_BAUD 115200

#if REVIX_OPERATING_MODE != REVIX_USER_MODE_DEMO && \
    REVIX_OPERATING_MODE != REVIX_USER_MODE_BENCH_LISTEN_ONLY
#error "REVIX_OPERATING_MODE must be DEMO or BENCH_LISTEN_ONLY"
#endif

#if REVIX_LED_DATA_GPIO < 0 || REVIX_LED_DATA_GPIO > 33 || \
    (REVIX_LED_DATA_GPIO >= 6 && REVIX_LED_DATA_GPIO <= 11)
#error "REVIX_LED_DATA_GPIO must be an exposed ESP32 output-capable GPIO"
#endif
#if REVIX_LED_COUNT < 1 || REVIX_LED_COUNT > 255
#error "REVIX_LED_COUNT must be between 1 and 255"
#endif
#if REVIX_LED_BRIGHTNESS < 0 || REVIX_LED_BRIGHTNESS > 255
#error "REVIX_LED_BRIGHTNESS must be between 0 and 255"
#endif

#if REVIX_OLED_ENABLED != 0 && REVIX_OLED_ENABLED != 1
#error "REVIX_OLED_ENABLED must be 0 or 1"
#endif
#if REVIX_OLED_SDA_GPIO < 0 || REVIX_OLED_SDA_GPIO > 33 || \
    (REVIX_OLED_SDA_GPIO >= 6 && REVIX_OLED_SDA_GPIO <= 11)
#error "REVIX_OLED_SDA_GPIO must be an exposed ESP32 GPIO"
#endif
#if REVIX_OLED_SCL_GPIO < 0 || REVIX_OLED_SCL_GPIO > 33 || \
    (REVIX_OLED_SCL_GPIO >= 6 && REVIX_OLED_SCL_GPIO <= 11)
#error "REVIX_OLED_SCL_GPIO must be an exposed ESP32 GPIO"
#endif
#if REVIX_OLED_ENABLED && REVIX_OLED_SDA_GPIO == REVIX_OLED_SCL_GPIO
#error "OLED SDA and SCL must use different GPIO pins"
#endif

#if REVIX_CAN_RX_GPIO < 0 || REVIX_CAN_RX_GPIO > 33 || \
    (REVIX_CAN_RX_GPIO >= 6 && REVIX_CAN_RX_GPIO <= 11)
#error "REVIX_CAN_RX_GPIO must be an exposed ESP32 GPIO"
#endif
#if REVIX_CAN_TX_GPIO < 0 || REVIX_CAN_TX_GPIO > 33 || \
    (REVIX_CAN_TX_GPIO >= 6 && REVIX_CAN_TX_GPIO <= 11)
#error "REVIX_CAN_TX_GPIO must be an exposed output-capable ESP32 GPIO"
#endif
#if REVIX_CAN_RX_GPIO == REVIX_CAN_TX_GPIO
#error "CAN RX and TX must use different GPIO pins"
#endif
#if REVIX_LED_DATA_GPIO == REVIX_CAN_RX_GPIO || REVIX_LED_DATA_GPIO == REVIX_CAN_TX_GPIO
#error "LED data and CAN must use different GPIO pins"
#endif
#if REVIX_OLED_ENABLED && \
    (REVIX_OLED_SDA_GPIO == REVIX_LED_DATA_GPIO || REVIX_OLED_SCL_GPIO == REVIX_LED_DATA_GPIO || \
     REVIX_OLED_SDA_GPIO == REVIX_CAN_RX_GPIO || REVIX_OLED_SDA_GPIO == REVIX_CAN_TX_GPIO || \
     REVIX_OLED_SCL_GPIO == REVIX_CAN_RX_GPIO || REVIX_OLED_SCL_GPIO == REVIX_CAN_TX_GPIO)
#error "Enabled OLED pins must not conflict with LED or CAN pins"
#endif
#if REVIX_CAN_BITRATE != 0 && REVIX_CAN_BITRATE != 125000 && \
    REVIX_CAN_BITRATE != 250000 && REVIX_CAN_BITRATE != 500000 && \
    REVIX_CAN_BITRATE != 1000000
#error "REVIX_CAN_BITRATE must be 0 or a supported TWAI bitrate"
#endif
#if REVIX_DEMO_MAX_RPM < 1 || REVIX_DEMO_MAX_RPM > 20000
#error "REVIX_DEMO_MAX_RPM must be between 1 and 20000"
#endif
#if REVIX_SERIAL_DIAGNOSTICS != 0 && REVIX_SERIAL_DIAGNOSTICS != 1
#error "REVIX_SERIAL_DIAGNOSTICS must be 0 or 1"
#endif
#if REVIX_SERIAL_BAUD < 1 || REVIX_SERIAL_BAUD > 2000000
#error "REVIX_SERIAL_BAUD must be between 1 and 2000000"
#endif

