#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/host
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/rpm_decoder.cpp src/vehicle_profile.cpp src/simulated_ecu.cpp src/shift_light.cpp test/test_core.cpp -o .pio/host/test_core
.pio/host/test_core

for diagnostics in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -DREVIX_OLED_ENABLED=1 -DREVIX_SERIAL_DIAGNOSTICS=$diagnostics -Itest/stubs -Iinclude src/rpm_decoder.cpp src/vehicle_profile.cpp src/simulated_ecu.cpp src/shift_light.cpp src/dashboard.cpp src/frame_log.cpp test/test_io.cpp -o .pio/host/test_io_$diagnostics
  .pio/host/test_io_$diagnostics
done

g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/rpm_decoder.cpp src/vehicle_profile.cpp src/shift_light.cpp test/test_vehicle.cpp -o .pio/host/test_vehicle
.pio/host/test_vehicle
python3 -m unittest discover -s test -p 'test_can_*.py'

for mode in 3 4; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -DREVIX_MODE=$mode -Itest/stubs -Iinclude src/can_transport.cpp src/frame_log.cpp src/rpm_decoder.cpp src/vehicle_profile.cpp test/test_transport.cpp -o .pio/host/test_transport_$mode
  .pio/host/test_transport_$mode
done

# Native-only selector wrapping exercises the actual vehicle config factory with
# qualified synthetic calibration without adding an override to vehicle firmware.
for mode in 1 4; do
  for diagnostics in 0 1; do
    g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -DREVIX_MODE=$mode -DREVIX_OLED_ENABLED=1 -DREVIX_SERIAL_DIAGNOSTICS=$diagnostics -Itest/stubs -Iinclude src/rpm_decoder.cpp src/vehicle_profile.cpp src/shift_light.cpp src/dashboard.cpp test/test_dashboard_calibration.cpp -Wl,--wrap=_Z22selectedVehicleProfilev -o .pio/host/test_dashboard-$mode-$diagnostics
    .pio/host/test_dashboard-$mode-$diagnostics
  done
done

# Release tests replace gh with a local recording stub; no publication occurs.
python3 -m unittest discover -s test -p "test_release_*.py"
