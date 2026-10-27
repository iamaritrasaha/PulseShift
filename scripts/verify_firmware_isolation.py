#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Check linked ESP32 images, not just source preprocessor conditions."""
from pathlib import Path
import os,subprocess,hashlib,json
root=Path(__file__).resolve().parents[1]
pio=Path(os.environ.get("PLATFORMIO_CORE_DIR",Path.home()/".platformio"))
nm=pio/"packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm"
results=[]
for environment in ("esp32dev_wokwi","esp32dev_bike","esp32dev_vehicle","esp32dev_twai_test"):
    elf=root/".pio/build"/environment/"firmware.elf"
    symbols=subprocess.check_output([str(nm),"-C",str(elf)],text=True)
    if environment=="esp32dev_wokwi":
        forbidden=("twai_transmit","CanTransport::","twai_driver_install")
    elif environment in ("esp32dev_bike","esp32dev_vehicle"):
        forbidden=("twai_transmit","CanTransport::sendTest","demonstrationFrame","ReplayTransport")
    else:
        forbidden=()
        assert "twai_transmit" in symbols and "CanTransport::sendTest" in symbols, "Self-test lost its isolated transmitter"
    assert not any(name in symbols for name in forbidden),f"Forbidden symbol in {environment}"
    results.append({"environment":environment,"sha256":hashlib.sha256(elf.read_bytes()).hexdigest(),"forbidden":forbidden,"passed":True})
print(json.dumps(results,indent=2))
