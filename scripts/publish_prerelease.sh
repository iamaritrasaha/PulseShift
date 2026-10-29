#!/usr/bin/env bash
# Copyright (c) 2026 Aritra Saha. All rights reserved.
# Called only by the explicitly approved tag-triggered publication workflow.
# Local regression tests replace gh with a recording stub; never use real gh
# while testing this script before publication approval.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TAG=$1
ASSETS=$(realpath -e -- "$2")
NOTES=${3:-"$ROOT/RELEASE_NOTES.md"}
VALIDATED=$(mktemp "${RUNNER_TEMP:-/tmp}/pulseshift-release-notes.XXXXXX.md")
trap 'rm -f -- "$VALIDATED"' EXIT
python3 "$ROOT/scripts/prepare_release_notes.py" --tag "$TAG" --notes "$NOTES" --output "$VALIDATED"
ZIP="$ASSETS/PulseShift-Arduino-$TAG.zip"
CHECKSUM="$ASSETS/PulseShift-Arduino-$TAG.sha256"
# The verifier rejects extra checksum lines, wrong filenames, unsafe ZIP paths,
# missing files and content mismatches before gh can create anything.
python3 "$ROOT/scripts/verify_arduino_package.py" --version "$TAG" --zip "$ZIP"
(cd -- "$ASSETS" && sha256sum --check -- "PulseShift-Arduino-$TAG.sha256")
# Create only. Existing releases are never edited and assets are never clobbered.
# --verify-tag prevents this command from implicitly creating a release tag.
gh release create "$TAG" "$ZIP" "$CHECKSUM" \
  --verify-tag --prerelease --latest=false \
  --title "PulseShift $TAG | Arduino IDE Software Prerelease" \
  --notes-file "$VALIDATED"
