#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Compile exact extracted Arduino ZIP variants in an unused Linux work folder."""
import argparse,subprocess,json,time,os
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--zip",type=Path,required=True);p.add_argument("--version",required=True)
p.add_argument("--work-dir",type=Path,required=True);p.add_argument("--ctags",type=Path)
p.add_argument("--arduino-cli",default="arduino-cli")
args=p.parse_args();root=Path(__file__).resolve().parents[1]
if args.work_dir.exists():p.error("--work-dir must not already exist")
args.work_dir.mkdir(parents=True)
variants={
 "demo-oled-off":{},
 "demo-oled-on":{"REVIX_OLED_ENABLED":"1"},
 "bench-oled-off":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_CAN_CAPTURE"},
 "bench-oled-on":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_CAN_CAPTURE","REVIX_OLED_ENABLED":"1"},
 "demo-serial-off":{"REVIX_SERIAL_DIAGNOSTICS":"0"},
 "demo-style-racing":{"REVIX_SHIFT_LIGHT_STYLE":"REVIX_STYLE_RACING"},
 "demo-startup-off":{"REVIX_STARTUP_ANIMATION_ENABLED":"0"},
 "vehicle-oled-off":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_VEHICLE_RPM"},
 "vehicle-oled-on":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_VEHICLE_RPM","REVIX_OLED_ENABLED":"1"},
 "vehicle-serial-off":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_VEHICLE_RPM","REVIX_SERIAL_DIAGNOSTICS":"0"},
 "bench-500k":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_CAN_CAPTURE","REVIX_CAN_BITRATE":"500000"},
 "bench-serial-off":{"REVIX_OPERATING_MODE":"REVIX_USER_MODE_CAN_CAPTURE","REVIX_CAN_BITRATE":"500000","REVIX_SERIAL_DIAGNOSTICS":"0"},
 "demo-minimal-reversed":{"REVIX_SHIFT_LIGHT_STYLE":"REVIX_STYLE_MINIMAL","REVIX_LED_DIRECTION":"REVIX_DIRECTION_REVERSED"},
 "demo-custom":{"REVIX_SHIFT_LIGHT_STYLE":"REVIX_STYLE_CUSTOM","REVIX_SHIFT_ALERT_MODE":"REVIX_ALERT_FLASH_WHITE","REVIX_IDLE_EFFECT_ENABLED":"1"},
 "demo-32":{"REVIX_LED_COUNT":"32","REVIX_LED_BRIGHTNESS":"255","REVIX_SMOOTH_TRANSITIONS_ENABLED":"0","REVIX_SHIFT_ALERT_MODE":"REVIX_ALERT_SOLID_RED"},
}
import re
pio=Path(os.environ.get("PLATFORMIO_CORE_DIR",Path.home()/".platformio"))
choices=[pio/"packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm"]
choices+=sorted((Path.home()/".arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc").glob("*/bin/xtensa-esp32-elf-nm"))
nm=next((path for path in choices if path.is_file()),None)
if nm is None:p.error("Xtensa nm is required from the installed PlatformIO or Arduino toolchain")
results=[]
for name,changes in variants.items():
    work=args.work_dir/name
    subprocess.run(["python3",str(root/"scripts/verify_arduino_package.py"),"--zip",str(args.zip.resolve()),
        "--version",args.version,"--extract-to",str(work.resolve())],check=True,stdout=subprocess.DEVNULL)
    config=work/"REVIX_LINK/UserConfig.h";text=config.read_text()
    for key,value in changes.items():
        text,count=re.subn(rf"(?m)^(#define {re.escape(key)} )[^\n]*",rf"\g<1>{value}",text)
        if count != 1:
            raise RuntimeError(f"Expected one configuration definition: {key}")
    config.write_text(text)
    command=[args.arduino_cli,"compile","--fqbn","esp32:esp32:esp32","--warnings","all",
        "--build-path",str((work/"build").resolve())]
    if args.ctags:command+=["--build-property",f"runtime.tools.ctags.path={args.ctags.resolve()}"]
    command.append(str((work/"REVIX_LINK").resolve()))
    start=time.monotonic()
    with (work/"compile.log").open("w") as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode==0:
        elves=list((work/"build").glob("*.elf"))
        if len(elves) != 1:
            raise RuntimeError(f"Expected one linked Arduino ELF for {name}")
        symbols=subprocess.check_output([str(nm),"-C",str(elves[0])],text=True)
        forbidden=("twai_transmit","CanTransport::sendTest","demonstrationFrame","ReplayTransport") if not name.startswith("demo-") else ("twai_transmit","CanTransport::","twai_driver_install")
        if any(symbol in symbols for symbol in forbidden):
            raise RuntimeError(f"Arduino mode isolation failed: {name}")
    results.append({"variant":name,"changes":changes,"returncode":result.returncode,"binary_isolation":result.returncode==0,"seconds":round(time.monotonic()-start,2),"command":command})
    (args.work_dir/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    print(f"{name}: {'PASS' if result.returncode==0 else 'FAIL'}",flush=True)
    if result.returncode:raise SystemExit(result.returncode)
