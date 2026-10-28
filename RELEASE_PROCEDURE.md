# v0.2.1 software prerelease publication procedure

This replaces the historical v0.2.0 preparation checklist, which referred to
an obsolete v0.1.0 README link and a previously published tag. Never overwrite
or move the existing v0.1.0 or v0.2.0 tags and release assets.

The v0.2.1 release contains the safety and reliability corrections reviewed in
PR #4. It remains a **software prerelease**: vehicle RPM, electrical passivity
of the CAN module, and the physical WS2812B data-level interface are unverified.

## Preparation and owner approval

1. Review and merge the v0.2.1 preparation pull request containing
   `RELEASE_NOTES.md`, `README.md`, and this procedure.
2. Confirm that the exact merge commit on `main` has a successful Firmware CI
   run, including all four PlatformIO builds, the native tests and the
   15-configuration extracted Arduino compile matrix.
3. Rebuild the v0.2.1 Arduino ZIP at that commit, verify the package, and
   calculate its checksum. Record the exact commit SHA, CI URL, and ZIP SHA-256
   in the owner approval record.
4. Do not push a tag until the owner has approved those precise values.
   A later `main` commit requires a fresh candidate and fresh approval.

All commands below run from the checkout in Ubuntu WSL.

### Verify the exact candidate

~~~sh
set -euo pipefail
cd /home/hrik/projects/REVIX_LINK_ESP32
git switch main
git pull --ff-only origin main
test -z "$(git status --porcelain)"
APPROVED_COMMIT=PASTE_FULL_APPROVED_MAIN_COMMIT_SHA
test "$(git rev-parse HEAD)" = "$APPROVED_COMMIT"
test "$(git ls-remote origin refs/heads/main | cut -f1)" = "$APPROVED_COMMIT"
python3 scripts/prepare_release_notes.py --tag v0.2.1 --notes RELEASE_NOTES.md
bash test/run.sh
~/.platformio/penv/bin/pio run -e esp32dev_wokwi -e esp32dev_twai_test -e esp32dev_bike -e esp32dev_vehicle
python3 scripts/verify_firmware_isolation.py
python3 -O scripts/verify_firmware_isolation.py
mkdir -p .pio/v0.2.1/candidate
python3 scripts/package_arduino.py --version v0.2.1 --output-dir .pio/v0.2.1/candidate
python3 scripts/verify_arduino_package.py --version v0.2.1 --zip .pio/v0.2.1/candidate/REVIX-LINK-Arduino-v0.2.1.zip
(cd .pio/v0.2.1/candidate && sha256sum --check REVIX-LINK-Arduino-v0.2.1.sha256)
sha256sum .pio/v0.2.1/candidate/REVIX-LINK-Arduino-v0.2.1.zip
~~~

Check the exact commit's GitHub Actions run, the Arduino compilation matrix,
package reproducibility and checksum before granting publication approval.
The existing CI workflow records those results. None of these commands
proves that a physical ESP32 or motorcycle interface works.

### Create the immutable v0.2.1 tag after explicit approval

Use the **same** `APPROVED_COMMIT` in the same shell. First confirm the remote
tag and release are absent. Never force-push a release tag or overwrite
an existing prerelease.

~~~sh
test -n "$APPROVED_COMMIT"
test "$(git rev-parse HEAD)" = "$APPROVED_COMMIT"
test -z "$(git tag --list v0.2.1)"
test -z "$(git ls-remote --tags origin refs/tags/v0.2.1)"
gh auth status
if gh release view v0.2.1 >/dev/null 2>&1; then
  echo "v0.2.1 release already exists: stop" >&2
  exit 1
fi
git tag -a v0.2.1 "$APPROVED_COMMIT" -m "REVIX LINK v0.2.1 Arduino IDE software prerelease"
git -c 'credential.helper=!gh auth git-credential' push origin refs/tags/v0.2.1
~~~

Pushing the tag starts `.github/workflows/release.yml`, whose notes gate,
full CI gate and create-only publisher are required. Do not manually create
a release to bypass a failing workflow.

## Postpublication verification

Wait for the exact tag-triggered workflow to succeed:

~~~sh
gh run list --workflow release.yml --limit 5 --json databaseId,headSha,status,conclusion,url
gh release view v0.2.1 --json tagName,isDraft,isPrerelease,assets,url
git fetch --no-tags origin tag v0.2.1
test "$(git cat-file -t refs/tags/v0.2.1)" = tag
test "$(git rev-parse 'v0.2.1^{commit}')" = "$APPROVED_COMMIT"
~~~

The release must be a nondraft prerelease with exactly the expected
versioned ZIP and checksum assets. Download them from the public URL and
verify their checksum and package contents:

~~~sh
mkdir -p .pio/v0.2.1/published
BASE=https://github.com/iamaritrasaha/REVIX-LINK/releases/download/v0.2.1
curl --fail --location --retry 3 "$BASE/REVIX-LINK-Arduino-v0.2.1.zip" -o .pio/v0.2.1/published/REVIX-LINK-Arduino-v0.2.1.zip
curl --fail --location --retry 3 "$BASE/REVIX-LINK-Arduino-v0.2.1.sha256" -o .pio/v0.2.1/published/REVIX-LINK-Arduino-v0.2.1.sha256
(cd .pio/v0.2.1/published && sha256sum --check REVIX-LINK-Arduino-v0.2.1.sha256)
test "$(sha256sum .pio/v0.2.1/published/REVIX-LINK-Arduino-v0.2.1.zip | cut -d ' ' -f1)" = "$(sha256sum .pio/v0.2.1/candidate/REVIX-LINK-Arduino-v0.2.1.zip | cut -d ' ' -f1)"
python3 scripts/verify_arduino_package.py --version v0.2.1 --zip .pio/v0.2.1/published/REVIX-LINK-Arduino-v0.2.1.zip
~~~

Run an extracted Arduino default compile and matrix again if the publication
candidate differs or the release verification requires it. Retain the
candidate hash and release CI evidence.

## README and About

The README now links to the **published releases list**, never to an unpublished
version. It flags that the v0.2.0 release predates the fixes and identifies
v0.2.1 or later as the audited version. After successful publication, verify
that the releases list displays v0.2.1 and its public assets.

After publication verification, the owner can update the GitHub About homepage
through GitHub CLI:

~~~sh
gh repo edit iamaritrasaha/REVIX-LINK --homepage "https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.2.1"
gh repo view iamaritrasaha/REVIX-LINK --json homepage --jq .homepage
~~~

The prior releases remain available for historical reproducibility. Successful
software publication does not qualify the motorcycle connection, actual
transceiver passivity, or direct 3.3V compatibility of the physical LED strip.
