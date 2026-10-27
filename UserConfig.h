// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// Standard: original ESP32 DevKit V1, six WS2812B pixels, direct data via 330 ohms.
// No settings need editing for the standalone demonstration. No CAN is needed.
// DEFAULT identifies the recommended value. OPTIONAL is a visual/user choice.
// CHANGE ONLY IF identifies a hardware difference, not a required setup step.
// DO NOT CHANGE WITHOUT VERIFICATION identifies bench/protocol safety settings.
// Symbolic choices below are constants: select a name, do not renumber it.
#define REVIX_USER_MODE_DEMO 1
#define REVIX_USER_MODE_CAN_CAPTURE 3
#define REVIX_USER_MODE_BENCH_LISTEN_ONLY REVIX_USER_MODE_CAN_CAPTURE // Compatibility alias.
#define REVIX_USER_MODE_VEHICLE_RPM 4
// DEFAULT: DEMO creates complete synthetic frames; it needs no CAN connection.
// DO NOT CHANGE WITHOUT VERIFICATION: CAN_CAPTURE requires a reviewed isolated
// bench and known bitrate. VEHICLE_RPM stays disabled until its entire profile
// and electrical qualification are verified. Neither mode sends CAN frames.
// PlatformIO bike/self-test environments force their own mode; see README.
#define REVIX_OPERATING_MODE REVIX_USER_MODE_DEMO

// DEFAULT: GPIO27 directly through 330 ohms to DIN; never connect it to +5V.
// Direct 3.3V data at 5V LED power requires a compatible strip; verify physically.
// CHANGE ONLY IF: LED wiring uses another verified output pin. Reserve 16/17 for CAN.
#define REVIX_LED_DATA_GPIO 27
// DEFAULT: 6 pixels. CHANGE ONLY IF: the physical chain differs; supported 1..32.
// Recheck supply/current capacity before increasing the count.
#define REVIX_LED_COUNT 6
// DEFAULT: 40/255. OPTIONAL: 0..255 brightness; 0 is dark, 255 is full scale.
// Software brightness is not a hardware current limiter.
#define REVIX_LED_BRIGHTNESS 40

// Symbolic style choices: green/yellow/orange/red, vivid racing colors,
// ice-white progression with red shift alert, or a custom RGB palette.
#define REVIX_STYLE_CLASSIC 1
#define REVIX_STYLE_RACING  2
#define REVIX_STYLE_MINIMAL 3
#define REVIX_STYLE_CUSTOM  4
// DEFAULT: CLASSIC. OPTIONAL: select RACING, MINIMAL or CUSTOM for visual preference.
#define REVIX_SHIFT_LIGHT_STYLE REVIX_STYLE_CLASSIC

// DEFAULT: six RGB triples, green through red. OPTIONAL: CUSTOM palette only.
// Supply 1..32 entries, channels 0..255; {0,0,0} intentionally stays dark.
// Extra pixels use fallback colors. Direction and brightness apply afterward.
// Triples are RGB colors; the fixed WS2812B wire format is GRB at 800kHz.
#define REVIX_CUSTOM_COLORS {0,255,0}, {0,255,0}, {255,200,0}, {255,200,0}, {255,80,0}, {255,0,0}

#define REVIX_DIRECTION_NORMAL   1
#define REVIX_DIRECTION_REVERSED 2
// DEFAULT: NORMAL fills from the first pixel at DIN. OPTIONAL: REVERSED changes
// visual order only; it does not swap DIN/DOUT or reverse the strip's data arrow.
#define REVIX_LED_DIRECTION REVIX_DIRECTION_NORMAL

// DEFAULT: 1 (enabled). OPTIONAL: 0 disables the separate startup sweep.
#define REVIX_STARTUP_ANIMATION_ENABLED 1
// DEFAULT: 1 (enabled). OPTIONAL: 0 gives stepped instead of smooth progression.
#define REVIX_SMOOTH_TRANSITIONS_ENABLED 1

// Symbolic alert choices. OFF retains full progression; flashes run at 10Hz.
#define REVIX_ALERT_OFF         0
#define REVIX_ALERT_SOLID_RED   1
#define REVIX_ALERT_FLASH_RED   2
#define REVIX_ALERT_FLASH_WHITE 3
// DEFAULT: FLASH_RED. OPTIONAL: OFF, SOLID_RED or FLASH_WHITE changes the effect.
#define REVIX_SHIFT_ALERT_MODE  REVIX_ALERT_FLASH_RED

// DEFAULT: 1500 RPM, first progression pixel. OPTIONAL: synthetic scale only;
// use 0 <= start < shift. No threshold here is a verified motorcycle setting.
#define REVIX_DEMO_START_RPM      1500
// DEFAULT: 5800 RPM, shift alert starts. OPTIONAL: start < shift <= maximum.
#define REVIX_DEMO_SHIFT_RPM      5800
// DEFAULT: 6500 RPM, synthetic decoding/scale limit. OPTIONAL: 1..20000 RPM,
// at least shift RPM. Above-range frames are rejected, not displayed as valid.
#define REVIX_DEMO_MAX_RPM        6500
// DEFAULT: 150 RPM. OPTIONAL: alert clears strictly below shift minus hysteresis.
// Use 0 <= hysteresis < (shift - start); zero is supported.
#define REVIX_ALERT_HYSTERESIS_RPM 150

// DEFAULT: 25ms (40Hz rendering). OPTIONAL: 5..250ms changes animation cadence.
#define REVIX_ANIMATION_INTERVAL_MS 25
// DEFAULT: 0 (off). OPTIONAL: 1 enables breathing at low validated/synthetic RPM.
#define REVIX_IDLE_EFFECT_ENABLED 0
// DEFAULT: 1300 RPM. OPTIONAL: idle-effect ceiling, 0..shift RPM; used if enabled.
#define REVIX_IDLE_MAX_RPM        1300

// DEFAULT: 0 (off) for the LED-only Arduino sketch. CHANGE ONLY IF: an SSD1306
// 128x64 at 0x3C is installed, then use 1 and the documented libraries.
// The existing Wokwi environment overrides this to 1 for its simulated OLED.
#ifndef REVIX_OLED_ENABLED
#define REVIX_OLED_ENABLED 0
#endif
// DEFAULT: GPIO21. CHANGE ONLY IF: optional OLED SDA is wired to another safe pin.
#define REVIX_OLED_SDA_GPIO 21
// DEFAULT: GPIO22. CHANGE ONLY IF: optional OLED SCL is wired to another safe pin.
#define REVIX_OLED_SCL_GPIO 22

// DEFAULT: GPIO16, reserved for transceiver RXD; leave unconnected in LED demo.
// DO NOT CHANGE WITHOUT VERIFICATION: receiver wiring and ESP32 pin suitability.
#define REVIX_CAN_RX_GPIO 16
// DEFAULT: GPIO17, reserved for transceiver TXD; leave unconnected in LED demo.
// DO NOT CHANGE WITHOUT VERIFICATION: reset/passivity protection. The bench
// firmware disconnects this output; actual module RS/TXD wiring must be checked.
#define REVIX_CAN_TX_GPIO 17
// DEFAULT: 0 keeps TWAI stopped. DO NOT CHANGE WITHOUT VERIFICATION: only a known
// isolated bench may use 125000, 250000, 500000 or 1000000 bits/s. Both the bike
// PlatformIO target and Arduino bench mode read this. It does not enable a
// motorcycle decoder or prove wiring, bitrate or passive RPM compatibility.
#define REVIX_CAN_BITRATE 0

// DEFAULT: 1 (on). OPTIONAL: 0 silences serial diagnostics and demo commands.
#ifndef REVIX_SERIAL_DIAGNOSTICS
#define REVIX_SERIAL_DIAGNOSTICS 1
#endif
// DEFAULT: 115200 baud. OPTIONAL: 1..2000000; match the monitor's baud setting.
// PlatformIO monitor_speed stays 115200 unless you change it separately.
#define REVIX_SERIAL_BAUD 115200

// Internal compile-time validation below. These are not user settings.
#if REVIX_OPERATING_MODE != REVIX_USER_MODE_DEMO && \
    REVIX_OPERATING_MODE != REVIX_USER_MODE_CAN_CAPTURE && \
    REVIX_OPERATING_MODE != REVIX_USER_MODE_VEHICLE_RPM
#error "REVIX_OPERATING_MODE must be DEMO, CAN_CAPTURE or VEHICLE_RPM"
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
