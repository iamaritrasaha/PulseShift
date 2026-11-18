#!/usr/bin/env bash
# Optional host-side C++ unit tests for PulseShift.
# Not required for normal Arduino IDE development. Run on Linux/macOS with g++.
# Used by GitHub Actions CI (see .github/workflows/ci.yml).
#
# Usage:  bash test/run.sh
# Requires: g++ with C++17, AddressSanitizer, Python 3.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${TMPDIR:-/tmp}/pulseshift-host-tests"
mkdir -p "$BUILD_DIR"

CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer"

# Core logic tests: config state machine, calibration binding, snapshot TLV
# serialization, event pacing and serial parsing (pure C++, no Arduino stubs;
# serial diagnostics are compiled out so no Serial symbol is referenced).
g++ $CXXFLAGS \
    -DPULSESHIFT_SERIAL_DIAGNOSTICS=0 \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_candidate.cpp PulseShift/src/config_manager.cpp \
    test/test_core.cpp -o "$BUILD_DIR/test_core"
"$BUILD_DIR/test_core"

# Unified LED engine tests: layer priorities, RPM validity safeguard,
# hysteresis, warning patterns, direction/brightness.
g++ $CXXFLAGS \
    -DPULSESHIFT_SERIAL_DIAGNOSTICS=0 \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_candidate.cpp PulseShift/src/config_manager.cpp \
    PulseShift/src/led_effects.cpp \
    test/test_led_effects.cpp -o "$BUILD_DIR/test_led_effects"
"$BUILD_DIR/test_led_effects"

# LedController tests: tri-state buffer architecture, transmission failure recovery,
# periodic complete-frame refresh, mock driver assertions.
g++ $CXXFLAGS \
    -DPULSESHIFT_TEST_BUILD -DPULSESHIFT_SERIAL_DIAGNOSTICS=0 \
    -Itest/stubs -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_candidate.cpp PulseShift/src/config_manager.cpp \
    PulseShift/src/led_effects.cpp PulseShift/src/led_driver.cpp \
    PulseShift/src/led_controller.cpp \
    test/test_led_controller.cpp -o "$BUILD_DIR/test_led_controller"
"$BUILD_DIR/test_led_controller"

# Compile the production RMT driver against the 3.3.12 HAL behavior fixture.
g++ $CXXFLAGS \
    -DARDUINO -DESP32 -Itest/stubs/rmt -IPulseShift/src \
    PulseShift/src/led_driver.cpp test/test_led_rmt_driver.cpp -o "$BUILD_DIR/test_led_rmt_driver"
"$BUILD_DIR/test_led_rmt_driver"

# BLE v2 command dispatch tests through the real BleManager dispatcher
# (auth binding, staged transactions, calibration workflow, device commands).
g++ $CXXFLAGS \
    -DPULSESHIFT_TEST_BUILD -DPULSESHIFT_SERIAL_DIAGNOSTICS=1 -DPULSESHIFT_USE_MBEDTLS \
    -Itest/stubs -IPulseShift -IPulseShift/src \
    PulseShift/src/ble_manager.cpp PulseShift/src/config_manager.cpp \
    PulseShift/src/rpm_candidate.cpp PulseShift/src/can_transport.cpp \
    PulseShift/src/frame_log.cpp PulseShift/src/can_recorder.cpp \
    PulseShift/src/storage_manager.cpp PulseShift/src/ota_manager.cpp \
    test/test_ble_commands.cpp -lmbedcrypto -o "$BUILD_DIR/test_ble_commands"
"$BUILD_DIR/test_ble_commands"

# Passive experimental 0x303[1] voltage hypothesis: byte validity and stale handling.
g++ $CXXFLAGS \
    -IPulseShift -IPulseShift/src \
    test/test_experimental_voltage_probe.cpp -o "$BUILD_DIR/test_experimental_voltage_probe"
"$BUILD_DIR/test_experimental_voltage_probe"

# Candidate 0x301 monitor tests
g++ $CXXFLAGS \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_candidate.cpp test/test_candidate_monitor.cpp \
    -o "$BUILD_DIR/test_candidate_monitor"
"$BUILD_DIR/test_candidate_monitor"

# Regression: RPM serial output must be present when enabled, and silent when disabled.
# Compile candidate monitor as its own translation unit (same as Arduino), not
# by including its source inside the sketch, to catch missing configuration headers.
for diagnostics in 0 1; do
  g++ $CXXFLAGS \
      -DPULSESHIFT_SERIAL_DIAGNOSTICS=$diagnostics \
      -IPulseShift -IPulseShift/src \
      PulseShift/src/rpm_candidate.cpp test/test_rpm_serial_output.cpp \
      -o "$BUILD_DIR/test_rpm_serial_output_$diagnostics"
  output="$("$BUILD_DIR/test_rpm_serial_output_$diagnostics")"
  if [[ "$diagnostics" == "1" ]]; then
    [[ "$output" == *"PULSESHIFT RPM MONITOR"* ]] || { echo "Missing RPM STATUS output" >&2; exit 1; }
    [[ "$output" == *"[RPM] Waiting for CAN ID"* ]] || { echo "Missing waiting-state RPM output" >&2; exit 1; }
    [[ "$output" == *"[RPM] Raw:"* ]] || { echo "Missing live RPM stream output" >&2; exit 1; }
  else
    [[ -z "$output" ]] || { echo "RPM diagnostics unexpectedly printed with diagnostics disabled" >&2; exit 1; }
  fi
done

# CAN transport tests: listen-only routing, TX-pin isolation, overflow handling.
g++ $CXXFLAGS \
    -DPULSESHIFT_SERIAL_DIAGNOSTICS=1 \
    -Itest/stubs -IPulseShift -IPulseShift/src \
    PulseShift/src/can_transport.cpp PulseShift/src/frame_log.cpp \
    test/test_transport.cpp -o "$BUILD_DIR/test_transport"
"$BUILD_DIR/test_transport"

# OTA cryptographic verification and fault-injection tests
g++ $CXXFLAGS \
    -DPULSESHIFT_TEST_BUILD -DPULSESHIFT_USE_MBEDTLS \
    -Itest/stubs -IPulseShift -IPulseShift/src \
    PulseShift/src/ota_manager.cpp PulseShift/src/can_transport.cpp \
    PulseShift/src/frame_log.cpp PulseShift/src/rpm_candidate.cpp \
    test/test_ota_verification.cpp -lmbedcrypto -o "$BUILD_DIR/test_ota_verification"
"$BUILD_DIR/test_ota_verification"

# Python integration tests
python3 -m unittest discover -s test -p 'test_*.py'

echo "All tests passed."
