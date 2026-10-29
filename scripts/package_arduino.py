#!/usr/bin/env python3
"""Create a deterministic Arduino IDE source distribution ZIP."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import stat
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SKETCH_NAME = "PulseShift"
VERSION_RE = re.compile(r"^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SOURCE_FILES = (
    "src/main.cpp",
    "src/can_transport.cpp",
    "src/rpm_decoder.cpp",
    "src/vehicle_profile.cpp",
    "src/dashboard.cpp",
    "src/frame_log.cpp",
    "src/shift_light.cpp",
    "src/simulated_ecu.cpp",
    "include/can_frame.h",
    "include/demo_commands.h",
    "include/event_summary.h",
    "include/can_transport.h",
    "include/config.h",
    "include/dashboard.h",
    "include/dashboard_model.h",
    "include/frame_log.h",
    "include/rpm_decoder.h",
    "include/vehicle_profile.h",
    "include/shift_light.h",
    "include/simulated_ecu.h",
    "include/tx_tracker.h",
)
REQUIRED_PACKAGE_FILES = {
    f"{SKETCH_NAME}/{SKETCH_NAME}.ino",
    f"{SKETCH_NAME}/UserConfig.h",
    f"{SKETCH_NAME}/ConfigCompat.h",
    f"{SKETCH_NAME}/README.txt",
    f"{SKETCH_NAME}/COPYRIGHT",
    *(f"{SKETCH_NAME}/src/{Path(name).name}" for name in SOURCE_FILES),
}


def fail(message: str) -> "NoReturn":
    print(f"package_arduino.py: error: {message}", file=sys.stderr)
    raise SystemExit(2)


def normalized_version(value: str) -> str:
    match = VERSION_RE.fullmatch(value)
    if not match:
        fail("version must use semantic version form X.Y.Z or vX.Y.Z")
    return "v" + ".".join(match.groups())


def stage_sketch(destination: Path, version: str) -> None:
    destination.mkdir(parents=True)
    (destination / "src").mkdir()
    (destination / f"{SKETCH_NAME}.ino").write_text(
        "// Copyright (c) 2026 Aritra Saha. All rights reserved.\n"
        "// Firmware entry points are defined once in src/main.cpp.\n",
        encoding="utf-8",
        newline="\n",
    )
    user_config = ROOT / "UserConfig.h"
    if not user_config.is_file() or user_config.is_symlink():
        fail("required user configuration is missing or is a symlink: UserConfig.h")
    shutil.copyfile(user_config, destination / "UserConfig.h")
    compat = ROOT / "ConfigCompat.h"
    if not compat.is_file() or compat.is_symlink():
        fail("required configuration compatibility header is missing or a symlink")
    shutil.copyfile(compat, destination / "ConfigCompat.h")
    copyright_file = ROOT / "COPYRIGHT"
    if not copyright_file.is_file() or copyright_file.is_symlink():
        fail("required copyright notice is missing or is a symlink: COPYRIGHT")
    shutil.copyfile(copyright_file, destination / "COPYRIGHT")

    template = ROOT / "arduino" / "README.txt"
    if not template.is_file():
        fail(f"missing package instructions: {template.relative_to(ROOT)}")
    (destination / "README.txt").write_text(
        template.read_text(encoding="utf-8").replace("@VERSION@", version),
        encoding="utf-8",
        newline="\n",
    )

    missing = []
    for relative in SOURCE_FILES:
        source = ROOT / relative
        if source.is_symlink() or not source.is_file():
            missing.append(relative)
            continue
        shutil.copyfile(source, destination / "src" / Path(relative).name)
    if missing:
        fail("required source files are missing or symlinks: " + ", ".join(missing))

    staged_files = {p.relative_to(destination.parent).as_posix() for p in destination.rglob("*") if p.is_file()}
    absent = sorted(REQUIRED_PACKAGE_FILES - staged_files)
    if absent:
        fail("staged sketch is incomplete: " + ", ".join(absent))

    sketch = destination / f"{SKETCH_NAME}.ino"
    ino = sketch.read_text(encoding="utf-8")
    if re.search(r"#\s*include\s*[<\"](?:\.\./)?[^>\"]+\.cpp[>\"]", ino):
        fail("the .ino must not include .cpp files")
    if re.search(r"\b(?:void\s+)?(?:setup|loop)\s*\(", ino):
        fail("the .ino must not define setup() or loop()")

    source_text = "\n".join(p.read_text(encoding="utf-8") for p in (destination / "src").glob("*.cpp"))
    for entrypoint in ("setup", "loop"):
        count = len(re.findall(r"\bvoid\s+" + entrypoint + r"\s*\(\s*\)", source_text))
        if count != 1:
            fail(f"expected exactly one {entrypoint}() definition, found {count}")


def write_deterministic_zip(sketch_dir: Path, zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_STORED) as archive:
        for source in sorted(p for p in sketch_dir.rglob("*") if p.is_file()):
            relative = source.relative_to(sketch_dir.parent).as_posix()
            info = zipfile.ZipInfo(relative, date_time=(2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_STORED
            info.flag_bits |= 0x800
            archive.writestr(info, source.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="release version, for example v1.0.0")
    parser.add_argument("--output-dir", required=True, type=Path, help="directory for ZIP and SHA-256 file")
    args = parser.parse_args()
    version = normalized_version(args.version)
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    asset_stem = f"PulseShift-Arduino-{version}"
    zip_path = output_dir / f"{asset_stem}.zip"
    checksum_path = output_dir / f"{asset_stem}.sha256"

    with tempfile.TemporaryDirectory(prefix="pulseshift-arduino-") as temp_dir:
        stage_root = Path(temp_dir)
        sketch_dir = stage_root / SKETCH_NAME
        stage_sketch(sketch_dir, version)
        temporary_zip = stage_root / zip_path.name
        write_deterministic_zip(sketch_dir, temporary_zip)
        digest = hashlib.sha256(temporary_zip.read_bytes()).hexdigest()
        temporary_checksum = stage_root / checksum_path.name
        temporary_checksum.write_text(f"{digest}  {zip_path.name}\n", encoding="ascii", newline="\n")
        shutil.copyfile(temporary_zip, zip_path)
        shutil.copyfile(temporary_checksum, checksum_path)

    print(f"Created {zip_path}")
    print(f"Created {checksum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
