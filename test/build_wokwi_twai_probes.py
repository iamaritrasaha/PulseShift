#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Compile isolated synthetic transmit probes. Never use these on a vehicle."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
VARIANTS={
    "single-shot": (1,0,0),
    "retry-enabled": (0,0,0),
    "queued": (1,5,0),
    "reference-aligned": (0,5,1),
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-cli",default="arduino-cli")
    parser.add_argument("--output-dir",type=Path,default=ROOT/".pio/twai-probes")
    args=parser.parse_args()
    versions=json.loads(subprocess.check_output([args.arduino_cli,"core","list","--format","json"],text=True))
    installed=versions.get("platforms",[]) if isinstance(versions,dict) else versions
    assert any(x.get("id")=="esp32:esp32" and x.get("installed_version",x.get("installed"))=="2.0.17" for x in installed), "Requires ESP32 core 2.0.17"
    output=args.output_dir.resolve()
    output.mkdir(parents=True,exist_ok=True)
    results=[]
    for name,(single,queue,reference) in VARIANTS.items():
        folder=output/name
        sketch=folder/"twai_probe"
        sketch.mkdir(parents=True,exist_ok=True)
        (sketch/"twai_probe.ino").write_bytes((ROOT/"test/wokwi_twai_probe.cpp").read_bytes())
        flags=f"-DPULSESHIFT_WOKWI_TWAI_PROBE=1 -DPROBE_SINGLE_SHOT={single} -DPROBE_TX_QUEUE={queue} -DPROBE_REFERENCE={reference}"
        cmd=[args.arduino_cli,"compile","--fqbn","esp32:esp32:esp32","--warnings","all",
             "--build-property","compiler.cpp.extra_flags="+flags,
             "--build-path",str(folder/"build"),str(sketch)]
        result=subprocess.run(cmd,capture_output=True,text=True)
        (folder/"build.log").write_text(result.stdout+result.stderr)
        results.append({"variant":name,"command":cmd,"returncode":result.returncode})
        print(name, "PASS" if result.returncode==0 else "FAIL",flush=True)
        (output/"build-results.json").write_text(json.dumps(results,indent=2)+"\n")
        if result.returncode: raise SystemExit(result.returncode)

if __name__=="__main__":
    main()
