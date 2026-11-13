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

# Core logic tests (no Arduino stubs needed)
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DPULSESHIFT_TEST_BUILD -DPULSESHIFT_MODE=1 \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_decoder.cpp PulseShift/src/vehicle_profile.cpp \
    PulseShift/src/simulated_ecu.cpp PulseShift/src/shift_light.cpp \
    test/test_core.cpp -o "$BUILD_DIR/test_core"

g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/shift_light.cpp test/test_shift_refresh.cpp \
    -o "$BUILD_DIR/test_shift_refresh"

"$BUILD_DIR/test_shift_refresh"
"$BUILD_DIR/test_core"

# I/O and dashboard tests (requires Arduino stubs)
for diagnostics in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      -DPULSESHIFT_TEST_BUILD -DPULSESHIFT_MODE=1 \
      -DPULSESHIFT_OLED_ENABLED=1 -DPULSESHIFT_SERIAL_DIAGNOSTICS=$diagnostics \
      -Itest/stubs -IPulseShift -IPulseShift/src \
      PulseShift/src/rpm_decoder.cpp PulseShift/src/vehicle_profile.cpp \
      PulseShift/src/simulated_ecu.cpp PulseShift/src/shift_light.cpp \
      PulseShift/src/led_effects.cpp PulseShift/src/config_manager.cpp \
      PulseShift/src/dashboard.cpp PulseShift/src/frame_log.cpp \
      test/test_io.cpp -o "$BUILD_DIR/test_io_$diagnostics"
  "$BUILD_DIR/test_io_$diagnostics"
done

# Vehicle profile and RPM tests
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_decoder.cpp PulseShift/src/vehicle_profile.cpp \
    PulseShift/src/shift_light.cpp test/test_vehicle.cpp \
    -o "$BUILD_DIR/test_vehicle"
"$BUILD_DIR/test_vehicle"

# Candidate 0x301 monitor tests
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -IPulseShift -IPulseShift/src \
    PulseShift/src/rpm_candidate.cpp test/test_candidate_monitor.cpp \
    -o "$BUILD_DIR/test_candidate_monitor"
"$BUILD_DIR/test_candidate_monitor"

# Regression: RPM serial output must be present when enabled, and silent when disabled.
# Compile candidate monitor as its own translation unit (same as Arduino), not
# by including its source inside the sketch, to catch missing configuration headers.
for diagnostics in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
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

# Python integration tests
python3 -m unittest discover -s test -p 'test_*.py'

# CAN transport tests (modes 3 and 4: CAN capture and vehicle)
for mode in 3 4; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      -DPULSESHIFT_MODE=$mode \
      -Itest/stubs -IPulseShift -IPulseShift/src \
      PulseShift/src/can_transport.cpp PulseShift/src/frame_log.cpp \
      PulseShift/src/rpm_decoder.cpp PulseShift/src/vehicle_profile.cpp \
      test/test_transport.cpp -o "$BUILD_DIR/test_transport_$mode"
  "$BUILD_DIR/test_transport_$mode"
done

# Dashboard calibration tests (modes 1 and 4)
for mode in 1 4; do
  for diagnostics in 0 1; do
    g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DPULSESHIFT_TEST_BUILD \
        -DPULSESHIFT_MODE=$mode -DPULSESHIFT_OLED_ENABLED=1 \
        -DPULSESHIFT_SERIAL_DIAGNOSTICS=$diagnostics \
        -Itest/stubs -IPulseShift -IPulseShift/src \
        PulseShift/src/rpm_decoder.cpp PulseShift/src/vehicle_profile.cpp \
        PulseShift/src/shift_light.cpp PulseShift/src/led_effects.cpp \
        PulseShift/src/config_manager.cpp PulseShift/src/dashboard.cpp \
        test/test_dashboard_calibration.cpp \
        -Wl,--wrap=_Z22selectedVehicleProfilev \
        -o "$BUILD_DIR/test_dashboard-$mode-$diagnostics"
    "$BUILD_DIR/test_dashboard-$mode-$diagnostics"
  done
done

echo "All tests passed."
