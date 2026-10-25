#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/host
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/rpm_decoder.cpp src/simulated_ecu.cpp test/test_core.cpp -o .pio/host/test_core
.pio/host/test_core
