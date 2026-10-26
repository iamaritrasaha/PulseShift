#!/usr/bin/env python3
"""Validate the layout and checksum of a generated Arduino ZIP."""

from __future__ import annotations

import argparse
import hashlib
import re
import stat
import sys
import zipfile
from pathlib import Path

from package_arduino import REQUIRED_PACKAGE_FILES, SKETCH_NAME, normalized_version


def fail(message: str) -> "NoReturn":
    print(f"verify_arduino_package.py: error: {message}", file=sys.stderr)
    raise SystemExit(2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", required=True, type=Path, help="generated Arduino ZIP")
    parser.add_argument("--version", required=True, help="expected version, for example v1.0.0")
    parser.add_argument("--extract-to", type=Path, help="extract to this previously absent directory")
    args = parser.parse_args()

    version = normalized_version(args.version)
    zip_path = args.zip.expanduser().resolve()
    expected_name = f"REVIX-LINK-Arduino-{version}.zip"
    if zip_path.name != expected_name:
        fail(f"expected archive name {expected_name}, got {zip_path.name}")
    checksum_path = zip_path.with_suffix(".sha256")
    if not checksum_path.is_file():
        fail(f"missing checksum file: {checksum_path.name}")
    expected_hash = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    checksum = checksum_path.read_text(encoding="ascii").strip()
    if checksum != f"{expected_hash}  {zip_path.name}":
        fail("SHA-256 checksum does not match the ZIP")

    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            fail("archive contains duplicate paths")
        if any(name.startswith("/") or ".." in Path(name).parts for name in names):
            fail("archive contains an unsafe path")
        missing = sorted(REQUIRED_PACKAGE_FILES - set(names))
        if missing:
            fail("archive is missing required files: " + ", ".join(missing))
        unexpected = sorted(set(names) - REQUIRED_PACKAGE_FILES)
        if unexpected:
            fail("archive contains unlisted files: " + ", ".join(unexpected))
        if any(not name.startswith(f"{SKETCH_NAME}/") for name in names):
            fail("archive must contain only the REVIX_LINK sketch directory")
        if sum(name.endswith(".ino") for name in names) != 1:
            fail("archive must contain exactly one .ino file")
        for info in archive.infolist():
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                fail(f"archive contains a symbolic link: {info.filename}")
        readme = archive.read(f"{SKETCH_NAME}/README.txt").decode("utf-8")
        if version not in readme:
            fail("README.txt does not contain the expected version")
        ino = archive.read(f"{SKETCH_NAME}/{SKETCH_NAME}.ino").decode("utf-8")
        if re.search(r"#\s*include\s*[<\"][^>\"]+\.cpp[>\"]", ino):
            fail(".ino must not include .cpp files")

        if args.extract_to:
            extract_to = args.extract_to.expanduser().resolve()
            if extract_to.exists():
                fail(f"extraction destination already exists: {extract_to}")
            extract_to.mkdir(parents=True)
            archive.extractall(extract_to)

    print(f"Validated {zip_path.name}: {len(names)} files, SHA-256 {expected_hash}")
    if args.extract_to:
        print(f"Extracted to {args.extract_to}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
