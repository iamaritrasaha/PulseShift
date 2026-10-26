// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// User settings for the ESP32 DevKit V1.
// Demo mode is safe to run without a motorcycle or CAN connection.
#define REVIX_USER_MODE_DEMO 1
#define REVIX_USER_MODE_BENCH_LISTEN_ONLY 3
#define REVIX_OPERATING_MODE REVIX_USER_MODE_DEMO

// Six WS2812B LEDs on the default REVIX LINK harness.
#define REVIX_LED_DATA_GPIO 27
#define REVIX_LED_COUNT 6  // 1..32; must match the physically installed chain.
#define REVIX_LED_BRIGHTNESS 40  // 0..255; master brightness scale.

// Shift-light style profile:
// 1 = CLASSIC (green -> yellow -> orange -> red)
// 2 = RACING  (vivid green -> bright amber -> crimson -> red)
// 3 = MINIMAL (clean ice white progressive fill with red shift point)
// 4 = CUSTOM  (user-defined palette)
#define REVIX_STYLE_CLASSIC 1
#define REVIX_STYLE_RACING  2
#define REVIX_STYLE_MINIMAL 3
#define REVIX_STYLE_CUSTOM  4
#define REVIX_SHIFT_LIGHT_STYLE REVIX_STYLE_CLASSIC

// CUSTOM profile: RGB triples in logical LED order, each channel 0..255.
// Supply 1..32 entries. Extra installed pixels use the engine's fallback colors.
// Explicit {0,0,0} entries remain dark; direction and brightness apply afterward.
#define REVIX_CUSTOM_COLORS {0,255,0}, {0,255,0}, {255,200,0}, {255,200,0}, {255,80,0}, {255,0,0}

// LED orientation:
// 1 = NORMAL   (left-to-right / ascending GPIO data order)
// 2 = REVERSED (right-to-left / descending order)
#define REVIX_DIRECTION_NORMAL   1
#define REVIX_DIRECTION_REVERSED 2
#define REVIX_LED_DIRECTION REVIX_DIRECTION_NORMAL

// Visual features:
#define REVIX_STARTUP_ANIMATION_ENABLED 1   // 1 = short sweep on boot; 0 = off.
#define REVIX_SMOOTH_TRANSITIONS_ENABLED 1  // 1 = smooth partial LED brightness; 0 = step-only.

// Shift alert behavior:
// 0 = OFF (alert disabled, progressive fill stays at maximum)
// 1 = SOLID_RED (all LEDs illuminate solid red)
// 2 = FLASH_RED (all LEDs flash red at 10 Hz)
// 3 = FLASH_WHITE (all LEDs flash white at 10 Hz)
#define REVIX_ALERT_OFF         0
#define REVIX_ALERT_SOLID_RED   1
#define REVIX_ALERT_FLASH_RED   2
#define REVIX_ALERT_FLASH_WHITE 3
#define REVIX_SHIFT_ALERT_MODE  REVIX_ALERT_FLASH_RED

// Synthetic demonstration RPM thresholds.
// Demonstration scale only; these are not verified motorcycle calibrations.
#define REVIX_DEMO_START_RPM      1500  // RPM at which the first shift LED begins illuminating.
#define REVIX_DEMO_SHIFT_RPM      5800  // Synthetic RPM at which shift alert activates.
#define REVIX_DEMO_MAX_RPM        6500  // Upper synthetic demonstration scale limit.
#define REVIX_ALERT_HYSTERESIS_RPM 150  // Clear strictly below shift minus this value; zero allowed.

// Animation rendering interval (milliseconds):
#define REVIX_ANIMATION_INTERVAL_MS 25  // 25 ms = 40 Hz render update rate.

// Optional engine idle breathing effect:
#define REVIX_IDLE_EFFECT_ENABLED 0     // Disabled by default for riding clarity.
#define REVIX_IDLE_MAX_RPM        1300  // Maximum RPM considered engine idle.

// Optional development display. Leave disabled for the LED-only device.
#ifndef REVIX_OLED_ENABLED
#define REVIX_OLED_ENABLED 0
#endif
#define REVIX_OLED_SDA_GPIO 21
#define REVIX_OLED_SCL_GPIO 22

// Isolated bench CAN capture only. 0 keeps the TWAI controller stopped.
// Select a nonzero rate only after verifying it on an isolated bench.
// Both PlatformIO bike builds and packaged Arduino builds read this value.
// This configures capture only: the NS125 RPM decoder remains disabled.
#define REVIX_CAN_RX_GPIO 16
#define REVIX_CAN_TX_GPIO 17
#define REVIX_CAN_BITRATE 0  // Supported: 0, 125000, 250000, 500000, 1000000.

// Serial Monitor diagnostics.
#ifndef REVIX_SERIAL_DIAGNOSTICS
#define REVIX_SERIAL_DIAGNOSTICS 1
#endif
#define REVIX_SERIAL_BAUD 115200

#if REVIX_OPERATING_MODE != REVIX_USER_MODE_DEMO && \
    REVIX_OPERATING_MODE != REVIX_USER_MODE_BENCH_LISTEN_ONLY
#error "REVIX_OPERATING_MODE must be DEMO or BENCH_LISTEN_ONLY"
#endif

#define REVIX_OUTPUT_GPIO_OK(pin) (((pin) >= 0 && (pin) <= 5) || \
    ((pin) >= 12 && (pin) <= 19) || ((pin) >= 21 && (pin) <= 23) || \
    ((pin) >= 25 && (pin) <= 27) || (pin) == 32 || (pin) == 33)
// GPIO6..11 are flash pins; GPIO20/24/28..31 do not exist on classic ESP32.
// GPIO0/2/5/12/15 are strapping pins: retain defaults unless the board is reviewed.
#if !REVIX_OUTPUT_GPIO_OK(REVIX_LED_DATA_GPIO)
#error "REVIX_LED_DATA_GPIO must be an exposed ESP32 output-capable GPIO"
#endif
#if REVIX_LED_COUNT < 1 || REVIX_LED_COUNT > 32
#error "REVIX_LED_COUNT must be between 1 and 32"
#endif
#if REVIX_LED_BRIGHTNESS < 0 || REVIX_LED_BRIGHTNESS > 255
#error "REVIX_LED_BRIGHTNESS must be between 0 and 255"
#endif

#if REVIX_SHIFT_LIGHT_STYLE < 1 || REVIX_SHIFT_LIGHT_STYLE > 4
#error "REVIX_SHIFT_LIGHT_STYLE must be CLASSIC, RACING, MINIMAL, or CUSTOM"
#endif
#if REVIX_LED_DIRECTION != REVIX_DIRECTION_NORMAL && REVIX_LED_DIRECTION != REVIX_DIRECTION_REVERSED
#error "REVIX_LED_DIRECTION must be NORMAL or REVERSED"
#endif
#if REVIX_STARTUP_ANIMATION_ENABLED != 0 && REVIX_STARTUP_ANIMATION_ENABLED != 1
#error "REVIX_STARTUP_ANIMATION_ENABLED must be 0 or 1"
#endif
#if REVIX_SMOOTH_TRANSITIONS_ENABLED != 0 && REVIX_SMOOTH_TRANSITIONS_ENABLED != 1
#error "REVIX_SMOOTH_TRANSITIONS_ENABLED must be 0 or 1"
#endif
#if REVIX_SHIFT_ALERT_MODE < 0 || REVIX_SHIFT_ALERT_MODE > 3
#error "REVIX_SHIFT_ALERT_MODE must be OFF, SOLID_RED, FLASH_RED, or FLASH_WHITE"
#endif

#if REVIX_DEMO_START_RPM < 0 || REVIX_DEMO_START_RPM >= REVIX_DEMO_SHIFT_RPM
#error "REVIX_DEMO_START_RPM must be non-negative and less than REVIX_DEMO_SHIFT_RPM"
#endif
#if REVIX_DEMO_SHIFT_RPM <= REVIX_DEMO_START_RPM || REVIX_DEMO_SHIFT_RPM > REVIX_DEMO_MAX_RPM
#error "REVIX_DEMO_SHIFT_RPM must be greater than REVIX_DEMO_START_RPM and <= REVIX_DEMO_MAX_RPM"
#endif
#if REVIX_ALERT_HYSTERESIS_RPM < 0 || REVIX_ALERT_HYSTERESIS_RPM >= (REVIX_DEMO_SHIFT_RPM - REVIX_DEMO_START_RPM)
#error "REVIX_ALERT_HYSTERESIS_RPM must be non-negative and less than the shift progression range"
#endif
#if REVIX_DEMO_MAX_RPM < 1 || REVIX_DEMO_MAX_RPM > 20000
#error "REVIX_DEMO_MAX_RPM must be between 1 and 20000"
#endif
#if REVIX_ANIMATION_INTERVAL_MS < 5 || REVIX_ANIMATION_INTERVAL_MS > 250
#error "REVIX_ANIMATION_INTERVAL_MS must be between 5 and 250 ms"
#endif
#if REVIX_IDLE_EFFECT_ENABLED != 0 && REVIX_IDLE_EFFECT_ENABLED != 1
#error "REVIX_IDLE_EFFECT_ENABLED must be 0 or 1"
#endif
#if REVIX_IDLE_MAX_RPM < 0 || REVIX_IDLE_MAX_RPM > REVIX_DEMO_SHIFT_RPM
#error "REVIX_IDLE_MAX_RPM must be between 0 and REVIX_DEMO_SHIFT_RPM"
#endif

#if REVIX_OLED_ENABLED != 0 && REVIX_OLED_ENABLED != 1
#error "REVIX_OLED_ENABLED must be 0 or 1"
#endif
#if !REVIX_OUTPUT_GPIO_OK(REVIX_OLED_SDA_GPIO)
#error "REVIX_OLED_SDA_GPIO must be an exposed ESP32 GPIO"
#endif
#if !REVIX_OUTPUT_GPIO_OK(REVIX_OLED_SCL_GPIO)
#error "REVIX_OLED_SCL_GPIO must be an exposed ESP32 GPIO"
#endif
#if REVIX_OLED_ENABLED && REVIX_OLED_SDA_GPIO == REVIX_OLED_SCL_GPIO
#error "OLED SDA and SCL must use different GPIO pins"
#endif

#if !(REVIX_OUTPUT_GPIO_OK(REVIX_CAN_RX_GPIO) || \
      (REVIX_CAN_RX_GPIO >= 34 && REVIX_CAN_RX_GPIO <= 36) || REVIX_CAN_RX_GPIO == 39)
#error "REVIX_CAN_RX_GPIO must be an exposed ESP32 GPIO"
#endif
#if !REVIX_OUTPUT_GPIO_OK(REVIX_CAN_TX_GPIO)
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
#if REVIX_SERIAL_DIAGNOSTICS != 0 && REVIX_SERIAL_DIAGNOSTICS != 1
#error "REVIX_SERIAL_DIAGNOSTICS must be 0 or 1"
#endif
#if REVIX_SERIAL_BAUD < 1 || REVIX_SERIAL_BAUD > 2000000
#error "REVIX_SERIAL_BAUD must be between 1 and 2000000"
#endif
