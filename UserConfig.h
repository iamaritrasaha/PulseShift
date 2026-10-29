// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// Standard: original ESP32 DevKit V1, six WS2812B pixels, direct data via 330 ohms.
// No settings need editing for the standalone demonstration. No CAN is needed.
// DEFAULT identifies the recommended value. OPTIONAL is a visual/user choice.
// CHANGE ONLY IF identifies a hardware difference, not a required setup step.
// DO NOT CHANGE WITHOUT VERIFICATION identifies bench/protocol safety settings.
// Symbolic choices below are constants: select a name, do not renumber it.
#define PULSESHIFT_USER_MODE_DEMO 1
#define PULSESHIFT_USER_MODE_CAN_CAPTURE 3
#define PULSESHIFT_USER_MODE_BENCH_LISTEN_ONLY PULSESHIFT_USER_MODE_CAN_CAPTURE // Compatibility alias.
#define PULSESHIFT_USER_MODE_VEHICLE_RPM 4
#define PULSESHIFT_STYLE_CLASSIC 1
#define PULSESHIFT_STYLE_RACING  2
#define PULSESHIFT_STYLE_MINIMAL 3
#define PULSESHIFT_STYLE_CUSTOM  4
#define PULSESHIFT_DIRECTION_NORMAL   1
#define PULSESHIFT_DIRECTION_REVERSED 2
#define PULSESHIFT_ALERT_OFF         0
#define PULSESHIFT_ALERT_SOLID_RED   1
#define PULSESHIFT_ALERT_FLASH_RED   2
#define PULSESHIFT_ALERT_FLASH_WHITE 3
#include "ConfigCompat.h"
// DEFAULT: DEMO creates complete synthetic frames; it needs no CAN connection.
// DO NOT CHANGE WITHOUT VERIFICATION: CAN_CAPTURE requires a reviewed isolated
// bench and known bitrate. VEHICLE_RPM stays disabled until its entire profile
// and electrical qualification are verified. Neither mode sends CAN frames.
// PlatformIO bike/self-test environments force their own mode; see README.
#ifndef PULSESHIFT_OPERATING_MODE
#define PULSESHIFT_OPERATING_MODE PULSESHIFT_USER_MODE_DEMO
#endif

// DEFAULT: GPIO27 directly through 330 ohms to DIN; never connect it to +5V.
// Direct 3.3V data at 5V LED power requires a compatible strip; verify physically.
// CHANGE ONLY IF: LED wiring uses another verified output pin. Reserve 16/17 for CAN.
#ifndef PULSESHIFT_LED_DATA_GPIO
#define PULSESHIFT_LED_DATA_GPIO 27
#endif
// DEFAULT: 6 pixels. CHANGE ONLY IF: the physical chain differs; supported 1..32.
// Recheck supply/current capacity before increasing the count.
#ifndef PULSESHIFT_LED_COUNT
#define PULSESHIFT_LED_COUNT 6
#endif
// DEFAULT: 40/255. OPTIONAL: 0..255 brightness; 0 is dark, 255 is full scale.
// Software brightness is not a hardware current limiter.
#ifndef PULSESHIFT_LED_BRIGHTNESS
#define PULSESHIFT_LED_BRIGHTNESS 40
#endif

// Symbolic style choices: green/yellow/orange/red, vivid racing colors,
// ice-white progression with red shift alert, or a custom RGB palette.
// DEFAULT: CLASSIC. OPTIONAL: select RACING, MINIMAL or CUSTOM for visual preference.
#ifndef PULSESHIFT_SHIFT_LIGHT_STYLE
#define PULSESHIFT_SHIFT_LIGHT_STYLE PULSESHIFT_STYLE_CLASSIC
#endif

// DEFAULT: six RGB triples, green through red. OPTIONAL: CUSTOM palette only.
// Supply 1..32 entries, channels 0..255; {0,0,0} intentionally stays dark.
// Extra pixels use fallback colors. Direction and brightness apply afterward.
// Triples are RGB colors; the fixed WS2812B wire format is GRB at 800kHz.
#ifndef PULSESHIFT_CUSTOM_COLORS
#define PULSESHIFT_CUSTOM_COLORS {0,255,0}, {0,255,0}, {255,200,0}, {255,200,0}, {255,80,0}, {255,0,0}
#endif

// DEFAULT: NORMAL fills from the first pixel at DIN. OPTIONAL: REVERSED changes
// visual order only; it does not swap DIN/DOUT or reverse the strip's data arrow.
#ifndef PULSESHIFT_LED_DIRECTION
#define PULSESHIFT_LED_DIRECTION PULSESHIFT_DIRECTION_NORMAL
#endif

// DEFAULT: 1 (enabled). OPTIONAL: 0 disables the separate startup sweep.
#ifndef PULSESHIFT_STARTUP_ANIMATION_ENABLED
#define PULSESHIFT_STARTUP_ANIMATION_ENABLED 1
#endif
// DEFAULT: 1 (enabled). OPTIONAL: 0 gives stepped instead of smooth progression.
#ifndef PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED
#define PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED 1
#endif

// Symbolic alert choices. OFF retains full progression; flashes run at 10Hz.
// DEFAULT: FLASH_RED. OPTIONAL: OFF, SOLID_RED or FLASH_WHITE changes the effect.
#ifndef PULSESHIFT_SHIFT_ALERT_MODE
#define PULSESHIFT_SHIFT_ALERT_MODE  PULSESHIFT_ALERT_FLASH_RED
#endif

// DEFAULT: 1500 RPM, first progression pixel. OPTIONAL: synthetic scale only;
// use 0 <= start < shift. No threshold here is a verified motorcycle setting.
#ifndef PULSESHIFT_DEMO_START_RPM
#define PULSESHIFT_DEMO_START_RPM      1500
#endif
// DEFAULT: 5800 RPM, shift alert starts. OPTIONAL: start < shift <= maximum.
#ifndef PULSESHIFT_DEMO_SHIFT_RPM
#define PULSESHIFT_DEMO_SHIFT_RPM      5800
#endif
// DEFAULT: 6500 RPM, synthetic decoding/scale limit. OPTIONAL: 1..20000 RPM,
// at least shift RPM. Above-range frames are rejected, not displayed as valid.
#ifndef PULSESHIFT_DEMO_MAX_RPM
#define PULSESHIFT_DEMO_MAX_RPM        6500
#endif
// DEFAULT: 150 RPM. OPTIONAL: alert clears strictly below shift minus hysteresis.
// Use 0 <= hysteresis < (shift - start); zero is supported.
#ifndef PULSESHIFT_ALERT_HYSTERESIS_RPM
#define PULSESHIFT_ALERT_HYSTERESIS_RPM 150
#endif

// DEFAULT: 25ms (40Hz rendering). OPTIONAL: 5..250ms changes animation cadence.
#ifndef PULSESHIFT_ANIMATION_INTERVAL_MS
#define PULSESHIFT_ANIMATION_INTERVAL_MS 25
#endif
// DEFAULT: 0 (off). OPTIONAL: 1 enables breathing at low validated/synthetic RPM.
#ifndef PULSESHIFT_IDLE_EFFECT_ENABLED
#define PULSESHIFT_IDLE_EFFECT_ENABLED 0
#endif
// DEFAULT: 1300 RPM. OPTIONAL: idle-effect ceiling, 0..shift RPM; used if enabled.
#ifndef PULSESHIFT_IDLE_MAX_RPM
#define PULSESHIFT_IDLE_MAX_RPM        1300
#endif

// DEFAULT: 0 (off) for the LED-only Arduino sketch. CHANGE ONLY IF: an SSD1306
// 128x64 at 0x3C is installed, then use 1 and the documented libraries.
// The existing Wokwi environment overrides this to 1 for its simulated OLED.
#ifndef PULSESHIFT_OLED_ENABLED
#define PULSESHIFT_OLED_ENABLED 0
#endif
// DEFAULT: GPIO21. CHANGE ONLY IF: optional OLED SDA is wired to another safe pin.
#ifndef PULSESHIFT_OLED_SDA_GPIO
#define PULSESHIFT_OLED_SDA_GPIO 21
#endif
// DEFAULT: GPIO22. CHANGE ONLY IF: optional OLED SCL is wired to another safe pin.
#ifndef PULSESHIFT_OLED_SCL_GPIO
#define PULSESHIFT_OLED_SCL_GPIO 22
#endif

// DEFAULT: GPIO16, reserved for transceiver RXD; leave unconnected in LED demo.
// DO NOT CHANGE WITHOUT VERIFICATION: receiver wiring and ESP32 pin suitability.
#ifndef PULSESHIFT_CAN_RX_GPIO
#define PULSESHIFT_CAN_RX_GPIO 16
#endif
// DEFAULT: GPIO17, reserved for transceiver TXD; leave unconnected in LED demo.
// DO NOT CHANGE WITHOUT VERIFICATION: reset/passivity protection. The bench
// firmware disconnects this output; actual module RS/TXD wiring must be checked.
#ifndef PULSESHIFT_CAN_TX_GPIO
#define PULSESHIFT_CAN_TX_GPIO 17
#endif
// DEFAULT: 0 keeps TWAI stopped. DO NOT CHANGE WITHOUT VERIFICATION: only a known
// isolated bench may use 125000, 250000, 500000 or 1000000 bits/s. Both the bike
// PlatformIO target and Arduino bench mode read this. It does not enable a
// motorcycle decoder or prove wiring, bitrate or passive RPM compatibility.
#ifndef PULSESHIFT_CAN_BITRATE
#define PULSESHIFT_CAN_BITRATE 0
#endif

// DEFAULT: 1 (on). OPTIONAL: 0 silences serial diagnostics and demo commands.
#ifndef PULSESHIFT_SERIAL_DIAGNOSTICS
#define PULSESHIFT_SERIAL_DIAGNOSTICS 1
#endif
// DEFAULT: 115200 baud. OPTIONAL: 1..2000000; match the monitor's baud setting.
// PlatformIO monitor_speed stays 115200 unless you change it separately.
#ifndef PULSESHIFT_SERIAL_BAUD
#define PULSESHIFT_SERIAL_BAUD 115200
#endif

// Export legacy names and reject old/new conflicts before validation.
#define PULSESHIFT_CONFIG_VALUES_READY 1
#include "ConfigCompat.h"

// Internal compile-time validation below. These are not user settings.
#if PULSESHIFT_OPERATING_MODE != PULSESHIFT_USER_MODE_DEMO && \
    PULSESHIFT_OPERATING_MODE != PULSESHIFT_USER_MODE_CAN_CAPTURE && \
    PULSESHIFT_OPERATING_MODE != PULSESHIFT_USER_MODE_VEHICLE_RPM
#error "PULSESHIFT_OPERATING_MODE must be DEMO, CAN_CAPTURE or VEHICLE_RPM"
#endif

#define PULSESHIFT_OUTPUT_GPIO_OK(pin) (((pin) >= 0 && (pin) <= 5) || \
    ((pin) >= 12 && (pin) <= 19) || ((pin) >= 21 && (pin) <= 23) || \
    ((pin) >= 25 && (pin) <= 27) || (pin) == 32 || (pin) == 33)
// GPIO6..11 are flash pins; GPIO20/24/28..31 do not exist on classic ESP32.
// GPIO0/2/5/12/15 are strapping pins: retain defaults unless the board is reviewed.
#if !PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_LED_DATA_GPIO)
#error "PULSESHIFT_LED_DATA_GPIO must be an exposed ESP32 output-capable GPIO"
#endif
#if PULSESHIFT_LED_COUNT < 1 || PULSESHIFT_LED_COUNT > 32
#error "PULSESHIFT_LED_COUNT must be between 1 and 32"
#endif
#if PULSESHIFT_LED_BRIGHTNESS < 0 || PULSESHIFT_LED_BRIGHTNESS > 255
#error "PULSESHIFT_LED_BRIGHTNESS must be between 0 and 255"
#endif

#if PULSESHIFT_SHIFT_LIGHT_STYLE < 1 || PULSESHIFT_SHIFT_LIGHT_STYLE > 4
#error "PULSESHIFT_SHIFT_LIGHT_STYLE must be CLASSIC, RACING, MINIMAL, or CUSTOM"
#endif
#if PULSESHIFT_LED_DIRECTION != PULSESHIFT_DIRECTION_NORMAL && PULSESHIFT_LED_DIRECTION != PULSESHIFT_DIRECTION_REVERSED
#error "PULSESHIFT_LED_DIRECTION must be NORMAL or REVERSED"
#endif
#if PULSESHIFT_STARTUP_ANIMATION_ENABLED != 0 && PULSESHIFT_STARTUP_ANIMATION_ENABLED != 1
#error "PULSESHIFT_STARTUP_ANIMATION_ENABLED must be 0 or 1"
#endif
#if PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED != 0 && PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED != 1
#error "PULSESHIFT_SMOOTH_TRANSITIONS_ENABLED must be 0 or 1"
#endif
#if PULSESHIFT_SHIFT_ALERT_MODE < 0 || PULSESHIFT_SHIFT_ALERT_MODE > 3
#error "PULSESHIFT_SHIFT_ALERT_MODE must be OFF, SOLID_RED, FLASH_RED, or FLASH_WHITE"
#endif

#if PULSESHIFT_DEMO_START_RPM < 0 || PULSESHIFT_DEMO_START_RPM >= PULSESHIFT_DEMO_SHIFT_RPM
#error "PULSESHIFT_DEMO_START_RPM must be non-negative and less than PULSESHIFT_DEMO_SHIFT_RPM"
#endif
#if PULSESHIFT_DEMO_SHIFT_RPM <= PULSESHIFT_DEMO_START_RPM || PULSESHIFT_DEMO_SHIFT_RPM > PULSESHIFT_DEMO_MAX_RPM
#error "PULSESHIFT_DEMO_SHIFT_RPM must be greater than PULSESHIFT_DEMO_START_RPM and <= PULSESHIFT_DEMO_MAX_RPM"
#endif
#if PULSESHIFT_ALERT_HYSTERESIS_RPM < 0 || PULSESHIFT_ALERT_HYSTERESIS_RPM >= (PULSESHIFT_DEMO_SHIFT_RPM - PULSESHIFT_DEMO_START_RPM)
#error "PULSESHIFT_ALERT_HYSTERESIS_RPM must be non-negative and less than the shift progression range"
#endif
#if PULSESHIFT_DEMO_MAX_RPM < 1 || PULSESHIFT_DEMO_MAX_RPM > 20000
#error "PULSESHIFT_DEMO_MAX_RPM must be between 1 and 20000"
#endif
#if PULSESHIFT_ANIMATION_INTERVAL_MS < 5 || PULSESHIFT_ANIMATION_INTERVAL_MS > 250
#error "PULSESHIFT_ANIMATION_INTERVAL_MS must be between 5 and 250 ms"
#endif
#if PULSESHIFT_IDLE_EFFECT_ENABLED != 0 && PULSESHIFT_IDLE_EFFECT_ENABLED != 1
#error "PULSESHIFT_IDLE_EFFECT_ENABLED must be 0 or 1"
#endif
#if PULSESHIFT_IDLE_MAX_RPM < 0 || PULSESHIFT_IDLE_MAX_RPM > PULSESHIFT_DEMO_SHIFT_RPM
#error "PULSESHIFT_IDLE_MAX_RPM must be between 0 and PULSESHIFT_DEMO_SHIFT_RPM"
#endif

#if PULSESHIFT_OLED_ENABLED != 0 && PULSESHIFT_OLED_ENABLED != 1
#error "PULSESHIFT_OLED_ENABLED must be 0 or 1"
#endif
#if !PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_OLED_SDA_GPIO)
#error "PULSESHIFT_OLED_SDA_GPIO must be an exposed ESP32 GPIO"
#endif
#if !PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_OLED_SCL_GPIO)
#error "PULSESHIFT_OLED_SCL_GPIO must be an exposed ESP32 GPIO"
#endif
#if PULSESHIFT_OLED_ENABLED && PULSESHIFT_OLED_SDA_GPIO == PULSESHIFT_OLED_SCL_GPIO
#error "OLED SDA and SCL must use different GPIO pins"
#endif

#if !(PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_CAN_RX_GPIO) || \
      (PULSESHIFT_CAN_RX_GPIO >= 34 && PULSESHIFT_CAN_RX_GPIO <= 36) || PULSESHIFT_CAN_RX_GPIO == 39)
#error "PULSESHIFT_CAN_RX_GPIO must be an exposed ESP32 GPIO"
#endif
#if !PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_CAN_TX_GPIO)
#error "PULSESHIFT_CAN_TX_GPIO must be an exposed output-capable ESP32 GPIO"
#endif
#if PULSESHIFT_CAN_RX_GPIO == PULSESHIFT_CAN_TX_GPIO
#error "CAN RX and TX must use different GPIO pins"
#endif
#if PULSESHIFT_LED_DATA_GPIO == PULSESHIFT_CAN_RX_GPIO || PULSESHIFT_LED_DATA_GPIO == PULSESHIFT_CAN_TX_GPIO
#error "LED data and CAN must use different GPIO pins"
#endif
#if PULSESHIFT_OLED_ENABLED && \
    (PULSESHIFT_OLED_SDA_GPIO == PULSESHIFT_LED_DATA_GPIO || PULSESHIFT_OLED_SCL_GPIO == PULSESHIFT_LED_DATA_GPIO || \
     PULSESHIFT_OLED_SDA_GPIO == PULSESHIFT_CAN_RX_GPIO || PULSESHIFT_OLED_SDA_GPIO == PULSESHIFT_CAN_TX_GPIO || \
     PULSESHIFT_OLED_SCL_GPIO == PULSESHIFT_CAN_RX_GPIO || PULSESHIFT_OLED_SCL_GPIO == PULSESHIFT_CAN_TX_GPIO)
#error "Enabled OLED pins must not conflict with LED or CAN pins"
#endif
#if PULSESHIFT_CAN_BITRATE != 0 && PULSESHIFT_CAN_BITRATE != 125000 && \
    PULSESHIFT_CAN_BITRATE != 250000 && PULSESHIFT_CAN_BITRATE != 500000 && \
    PULSESHIFT_CAN_BITRATE != 1000000
#error "PULSESHIFT_CAN_BITRATE must be 0 or a supported TWAI bitrate"
#endif
#if PULSESHIFT_SERIAL_DIAGNOSTICS != 0 && PULSESHIFT_SERIAL_DIAGNOSTICS != 1
#error "PULSESHIFT_SERIAL_DIAGNOSTICS must be 0 or 1"
#endif
#if PULSESHIFT_SERIAL_BAUD < 1 || PULSESHIFT_SERIAL_BAUD > 2000000
#error "PULSESHIFT_SERIAL_BAUD must be between 1 and 2000000"
#endif


// UART0 is used by Serial diagnostics on the original ESP32 DevKit V1.
// CHANGE ONLY IF: disabling diagnostics also requires checking USB-UART wiring.
#if PULSESHIFT_SERIAL_DIAGNOSTICS && \
    (PULSESHIFT_LED_DATA_GPIO == 1 || PULSESHIFT_LED_DATA_GPIO == 3 || \
     PULSESHIFT_CAN_RX_GPIO == 1 || PULSESHIFT_CAN_RX_GPIO == 3 || \
     PULSESHIFT_CAN_TX_GPIO == 1 || PULSESHIFT_CAN_TX_GPIO == 3 || \
     (PULSESHIFT_OLED_ENABLED && (PULSESHIFT_OLED_SDA_GPIO == 1 || PULSESHIFT_OLED_SDA_GPIO == 3 || \
                            PULSESHIFT_OLED_SCL_GPIO == 1 || PULSESHIFT_OLED_SCL_GPIO == 3)))
#error "Enabled Serial diagnostics reserve UART0 GPIO1 and GPIO3"
#endif
