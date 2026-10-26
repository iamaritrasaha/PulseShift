#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/host
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/rpm_decoder.cpp src/simulated_ecu.cpp src/shift_light.cpp test/test_core.cpp -o .pio/host/test_core
.pio/host/test_core

for diagnostics in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -DREVIX_OLED_ENABLED=1 -DREVIX_SERIAL_DIAGNOSTICS=$diagnostics -Itest/stubs -Iinclude src/rpm_decoder.cpp src/simulated_ecu.cpp src/shift_light.cpp src/dashboard.cpp src/frame_log.cpp test/test_io.cpp -o .pio/host/test_io_$diagnostics
  .pio/host/test_io_$diagnostics
done
