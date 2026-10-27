#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Reproduce artificial CAN/reference data, never an NS125 capture."""
from pathlib import Path
import csv,random
root=Path(__file__).resolve().parent
randomizer=random.Random(125)
cycle=[0,0,1400,1800,2200,3500,5000,6500,7200,6000,4500,2000,1400,0]
lines=["# SYNTHETIC FIXTURE ONLY. 0x612 and its encoding are NOT vehicle facts."]
references=[]
vectors=[]
for index in range(84):
    t=1000+index*100
    rpm=cycle[index%len(cycle)]
    raw=(rpm+100)*4
    data=bytearray([0x81,2,raw&255,raw>>8,index%16,randomizer.randrange(256),0xD3,0])
    data[7]=sum(data[:7])&255
    lines.append(f"RX t={t} id=00000612 ext=0 rtr=0 dlc=8 data={data.hex().upper()} UNMAPPED")
    references.append((t+20,rpm))
    vectors.append((t,"00000612",0,8,data.hex().upper(),rpm))
    unrelated=bytes(randomizer.randrange(256) for _ in range(8))
    lines.append(f"RX t={t+1} id=00000612 ext=1 rtr=0 dlc=8 data={unrelated.hex().upper()} UNMAPPED")
    lines.append(f"RX t={t+2} id=00000200 ext=0 rtr=0 dlc=2 data={index:02X}A5 UNMAPPED")
lines+=["RX t=9400 id=00000612 ext=0 rtr=1 dlc=8 data= RTR",
        "RX t=9500 id=00000612 ext=0 rtr=0 dlc=8 data=AABB MALFORMED",
        "CAN status=ESP_OK state=1 TEC=0 REC=0 missed=0 overrun=0 busErrors=0 RX=253 acceptedFrames=253",
        "APP mode=CAN_CAPTURE accepted=0 rejected=253 fresh=0 busFresh=1 rpm=0 logDropped=0"]
(root/"synthetic_capture.log").write_text("\n".join(lines)+"\n")
with (root/"synthetic_rpm.csv").open("w",newline="") as f:
    writer=csv.writer(f);writer.writerow(["timestamp_ms","rpm"]);writer.writerows(references)
with (root/"synthetic_vehicle.csv").open("w",newline="") as f:
    writer=csv.writer(f);writer.writerow(["timestamp_ms","id","extended","dlc","data","rpm"]);writer.writerows(vectors)
print("Rebuilt synthetic fixtures, not vehicle evidence.")
