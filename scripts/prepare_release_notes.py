#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Validate tag-specific preview notes before any publication side effect."""
import argparse
from pathlib import Path
import re

QUALIFICATIONS=(
    "Motorcycle RPM integration has not been physically qualified.",
    "The actual SN65HVD230 transceiver's electrical passivity has not been physically qualified.",
    "Direct 3.3V LED signal compatibility with the actual 5V strip has not been physically qualified.",
)
def validate(tag, text):
    if not re.fullmatch(r"v0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)",tag):
        raise ValueError("Only exact v0.x.y software prerelease tags are permitted")
    expected=f"# PulseShift {tag} | Arduino IDE software prerelease"
    if not text.splitlines() or text.splitlines()[0]!=expected:
        raise ValueError("RELEASE_NOTES.md heading must match the release tag exactly")
    normalized=" ".join(text.split())
    for statement in (*QUALIFICATIONS,"Wokwi TWAI transmission completion remains unverified."):
        if statement not in normalized:
            raise ValueError("Missing required qualification: "+statement)
    for mode in ("DEMO","CAN_CAPTURE","VEHICLE_RPM"):
        if mode not in text: raise ValueError("Missing firmware mode: "+mode)
    if f"PulseShift-Arduino-{tag}.zip" not in text:
        raise ValueError("Release notes must identify the versioned Arduino ZIP")
    if "\u2014" in text:
        raise ValueError("Use plain punctuation in release notes")
    return text

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag",required=True)
    parser.add_argument("--notes",type=Path,default=Path("RELEASE_NOTES.md"))
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    try: text=validate(args.tag,args.notes.read_text(encoding="utf-8"))
    except (ValueError,OSError) as error: parser.error(str(error))
    if args.output: args.output.write_text(text,encoding="utf-8")
    print(f"Validated version-specific notes for {args.tag}")
if __name__=="__main__":
    main()
