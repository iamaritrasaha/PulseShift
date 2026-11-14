// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// Standard: original ESP32 DevKit V1, six WS2812B pixels, direct data via 330 ohms.
// This file only holds hardware-level compile-time settings. All user-facing
// lighting / RPM-decoder settings live in NVS and are edited from the Android
// app over BLE; no recompile is needed to change them.
// DEFAULT identifies the recommended value. OPTIONAL is a visual/user choice.
// CHANGE ONLY IF identifies a hardware difference, not a required setup step.
// DO NOT CHANGE WITHOUT VERIFICATION identifies bench/protocol safety settings.

// DEFAULT: GPIO27 directly through 330 ohms to DIN; never connect it to +5V.
// Direct 3.3V data at 5V LED power requires a compatible strip; verify physically.
// CHANGE ONLY IF: LED wiring uses another verified output pin. Reserve 16/17 for CAN.
#ifndef PULSESHIFT_LED_DATA_GPIO
#define PULSESHIFT_LED_DATA_GPIO 27
#endif
// DEFAULT: 6 pixels. CHANGE ONLY IF: the physical chain differs; supported 1..6
// (the unified six-stage shift-light engine renders exactly six pixels).
// Recheck supply/current capacity before changing the count.
#ifndef PULSESHIFT_LED_COUNT
#define PULSESHIFT_LED_COUNT 6
#endif

// DEFAULT: GPIO16, reserved for transceiver RXD.
// DO NOT CHANGE WITHOUT VERIFICATION: receiver wiring and ESP32 pin suitability.
#ifndef PULSESHIFT_CAN_RX_GPIO
#define PULSESHIFT_CAN_RX_GPIO 16
#endif
// DEFAULT: GPIO17, reserved for transceiver TXD.
// DO NOT CHANGE WITHOUT VERIFICATION: reset/passivity protection. The firmware
// runs TWAI in listen-only mode and isolates this pin in input mode. On the
// physical hardware, ESP32 GPIO17 must remain physically disconnected, while
// breakout CTX/TXD connects directly to ESP32 3V3 (no resistor required) to
// guarantee zero bus transmission.
#ifndef PULSESHIFT_CAN_TX_GPIO
#define PULSESHIFT_CAN_TX_GPIO 17
#endif
// DEFAULT: 500000 bps, previously observed on the NS125. DO NOT CHANGE WITHOUT
// VERIFICATION: only a known isolated bench may use 125000, 250000, 500000 or
// 1000000 bits/s. It does not enable a motorcycle decoder or prove wiring,
// bitrate or passive RPM compatibility.
#ifndef PULSESHIFT_CAN_BITRATE
#define PULSESHIFT_CAN_BITRATE 500000
#endif

// DEFAULT: 1 (on). OPTIONAL: 0 silences serial diagnostics for fully
// headless operation (the Android app remains the complete command interface).
#ifndef PULSESHIFT_SERIAL_DIAGNOSTICS
#define PULSESHIFT_SERIAL_DIAGNOSTICS 1
#endif
// DEFAULT: 115200 baud. OPTIONAL: 1..2000000; match Arduino Serial Monitor baud rate.
#ifndef PULSESHIFT_SERIAL_BAUD
#define PULSESHIFT_SERIAL_BAUD 115200
#endif

// DEFAULT: GPIO0 (BOOT button on ESP32 DevKit V1).
// Short press cycles capture stages; long press toggles recording; any press
// opens the 60-second owner enrollment window (cyan pulse).
#ifndef PULSESHIFT_BOOT_BUTTON_GPIO
#define PULSESHIFT_BOOT_BUTTON_GPIO 0
#endif
// DEFAULT: 1000000 bytes (~1 MB). Max raw CAN capture file size in LittleFS.
#ifndef PULSESHIFT_MAX_CAPTURE_BYTES
#define PULSESHIFT_MAX_CAPTURE_BYTES 1000000
#endif
// DEFAULT: 131072 bytes (128 KB). Max persistent diagnostic event log size.
#ifndef PULSESHIFT_MAX_DIAG_LOG_BYTES
#define PULSESHIFT_MAX_DIAG_LOG_BYTES 131072
#endif

// DEFAULT: 1 (on). Enables BLE GATT peripheral for live Android app telemetry,
// configuration and diagnostics.
#ifndef PULSESHIFT_BLE_ENABLED
#define PULSESHIFT_BLE_ENABLED 1
#endif

// Export legacy names and reject old/new conflicts before validation.
#define PULSESHIFT_CONFIG_VALUES_READY 1

// Internal compile-time validation below. These are not user settings.
#define PULSESHIFT_OUTPUT_GPIO_OK(pin) (((pin) >= 0 && (pin) <= 5) || \
    ((pin) >= 12 && (pin) <= 19) || ((pin) >= 21 && (pin) <= 23) || \
    ((pin) >= 25 && (pin) <= 27) || (pin) == 32 || (pin) == 33)
// GPIO6..11 are flash pins; GPIO20/24/28..31 do not exist on classic ESP32.
// GPIO0/2/5/12/15 are strapping pins: retain defaults unless the board is reviewed.
#if !PULSESHIFT_OUTPUT_GPIO_OK(PULSESHIFT_LED_DATA_GPIO)
#error "PULSESHIFT_LED_DATA_GPIO must be an exposed ESP32 output-capable GPIO"
#endif
#if PULSESHIFT_LED_COUNT < 1 || PULSESHIFT_LED_COUNT > 6
#error "PULSESHIFT_LED_COUNT must be between 1 and 6"
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
#if PULSESHIFT_BLE_ENABLED != 0 && PULSESHIFT_BLE_ENABLED != 1
#error "PULSESHIFT_BLE_ENABLED must be 0 or 1"
#endif

// UART0 is used by Serial diagnostics on the original ESP32 DevKit V1.
// CHANGE ONLY IF: disabling diagnostics also requires checking USB-UART wiring.
#if PULSESHIFT_SERIAL_DIAGNOSTICS && \
    (PULSESHIFT_LED_DATA_GPIO == 1 || PULSESHIFT_LED_DATA_GPIO == 3 || \
     PULSESHIFT_CAN_RX_GPIO == 1 || PULSESHIFT_CAN_RX_GPIO == 3 || \
     PULSESHIFT_CAN_TX_GPIO == 1 || PULSESHIFT_CAN_TX_GPIO == 3)
#error "Enabled Serial diagnostics reserve UART0 GPIO1 and GPIO3"
#endif

// Fix D8: PSRAM conflict check on GPIO16/17 for ESP32-WROVER modules
// In Arduino-ESP32, BOARD_HAS_PSRAM is the official macro when PSRAM is enabled for the board.
#if defined(BOARD_HAS_PSRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && defined(CONFIG_SPIRAM) && !defined(ARDUINO))
#if (PULSESHIFT_CAN_RX_GPIO == 16 || PULSESHIFT_CAN_RX_GPIO == 17 || \
     PULSESHIFT_CAN_TX_GPIO == 16 || PULSESHIFT_CAN_TX_GPIO == 17)
#error "GPIO16/17 are reserved for external PSRAM on WROVER boards. Use non-PSRAM GPIOs or verify board is standard WROOM-32."
#endif
#endif
