# v0.2.0 publication procedure

All local commands run inside Ubuntu WSL from:
~~~
cd /home/hrik/projects/REVIX_LINK_ESP32
~~~

The current README and GitHub About homepage must keep the v0.1.0 link until
the new public assets have passed the postpublication checks below. The prepared
scripts/release/README-v0.2.0.patch is not applied during release preparation.

## Owner approval boundary

Stop before creating or pushing any tag. The final approval report supplies the
exact release-candidate commit, its successful CI run and the ZIP checksum.
Never substitute a later main commit without rebuilding and reapproval.

After explicit approval only, substitute that exact commit into APPROVED_COMMIT:

~~~sh
set -euo pipefail
mkdir -p .pio/v0.2.0
APPROVED_COMMIT=PASTE_THE_EXACT_APPROVED_COMMIT_SHA
test -z "$(git status --porcelain)"
test "$(git branch --show-current)" = main
test "$(git rev-parse HEAD)" = "$APPROVED_COMMIT"
test "$(git ls-remote origin refs/heads/main | cut -f1)" = "$APPROVED_COMMIT"
test -z "$(git tag --list v0.2.0)"
test -z "$(git ls-remote --tags origin refs/tags/v0.2.0)"
gh api repos/iamaritrasaha/REVIX-LINK/releases --paginate --jq '.[].tag_name' > .pio/v0.2.0/existing-release-tags.txt
if grep -Fxq v0.2.0 .pio/v0.2.0/existing-release-tags.txt; then
  echo "v0.2.0 release already exists; stop without overwriting it" >&2
  exit 1
fi
git tag -a v0.2.0 "$APPROVED_COMMIT" -m "REVIX LINK v0.2.0 Arduino IDE software prerelease"
git -c 'credential.helper=!gh auth git-credential' push origin refs/tags/v0.2.0
~~~

The tag push starts release.yml. Its notes gate accepts only v0.x.y tags whose
RELEASE_NOTES.md heading matches exactly. It then runs the full CI verification.
Publication uses those checked-in version-specific notes and verified assets.
The create-only publisher always sets prerelease, disables Latest, requires an
existing remote tag and never edits an existing release or clobbers assets.
A failure must be investigated; never force-update a tag or manually bypass
verification. No separate gh release create command is required.

The exact copy-ready tag commands, pinned to the final commit, are also prepared
locally in .pio/v0.2.0/publication-after-approval.sh. Do not execute that file
before explicit owner approval.

## Postpublication verification

In a new shell, set APPROVED_COMMIT to the same approved SHA before proceeding.
Do not update download links yet. First wait for the tag-triggered publication
workflow to complete successfully. Identify its exact run with:

~~~sh
gh run list --workflow release.yml --limit 5 --json databaseId,headSha,status,conclusion,url
~~~

Check the public release metadata and verify it is a non-draft prerelease:

~~~sh
mkdir -p .pio/v0.2.0
gh release view v0.2.0 --json tagName,isDraft,isPrerelease,body,assets,url > .pio/v0.2.0/published-release.json
python3 - <<'PY'
import json
from pathlib import Path
p=json.loads(Path(".pio/v0.2.0/published-release.json").read_text())
assert p["tagName"]=="v0.2.0" and not p["isDraft"] and p["isPrerelease"]
assert {a["name"] for a in p["assets"]}=={
    "REVIX-LINK-Arduino-v0.2.0.zip","REVIX-LINK-Arduino-v0.2.0.sha256"}
assert p["body"].rstrip()==Path("RELEASE_NOTES.md").read_text().rstrip()
PY
git fetch --no-tags origin tag v0.2.0
test "$(git cat-file -t refs/tags/v0.2.0)" = tag
test "$(git rev-parse 'v0.2.0^{commit}')" = "$APPROVED_COMMIT"
~~~

Download through unauthenticated public URLs, not a draft-only API asset:

~~~sh
VERIFY_DIR=$(mktemp -d "$PWD/.pio/v0.2.0/published.XXXXXX")
PUBLIC_BASE=https://github.com/iamaritrasaha/REVIX-LINK/releases/download/v0.2.0
curl --fail --location --retry 3 "$PUBLIC_BASE/REVIX-LINK-Arduino-v0.2.0.zip" -o "$VERIFY_DIR/REVIX-LINK-Arduino-v0.2.0.zip"
curl --fail --location --retry 3 "$PUBLIC_BASE/REVIX-LINK-Arduino-v0.2.0.sha256" -o "$VERIFY_DIR/REVIX-LINK-Arduino-v0.2.0.sha256"
(cd "$VERIFY_DIR" && sha256sum --check REVIX-LINK-Arduino-v0.2.0.sha256)
EXPECTED_ZIP_SHA256=3c4063eb1db18e79f894b730b53f3cd8d593fc6920fe966a68af8c86bb1bbca4
test "$(sha256sum "$VERIFY_DIR/REVIX-LINK-Arduino-v0.2.0.zip" | cut -d ' ' -f1)" = "$EXPECTED_ZIP_SHA256"
python3 scripts/verify_arduino_package.py --version v0.2.0 --zip "$VERIFY_DIR/REVIX-LINK-Arduino-v0.2.0.zip" --extract-to "$VERIFY_DIR/extracted"
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --build-path "$VERIFY_DIR/default-build" "$VERIFY_DIR/extracted/REVIX_LINK"
python3 scripts/verify_arduino_matrix.py --version v0.2.0 --zip "$VERIFY_DIR/REVIX-LINK-Arduino-v0.2.0.zip" --work-dir "$VERIFY_DIR/matrix" --arduino-cli "$HOME/.local/bin/arduino-cli"
~~~

Use the pinned ESP32 core 2.0.17 and libraries in the packaged README.txt.
The matrix recompiles all 15 supported configurations and checks ELF isolation.
No USB upload or motorcycle connection is part of these checks. Preserve the
download, checksum and build outputs for the publication record.

## Update links only after all checks pass

The prepared patch changes only the README release link, asset filename and
obsolete v0.1.0 bundled-guide warning. Wiring, diagrams and safety wording stay
intact. Prepare the link update on an ordinary branch:

~~~sh
git switch main
git pull --ff-only origin main
git switch -c release/v0.2.0-download-links
git apply --unidiff-zero --check scripts/release/README-v0.2.0.patch
git apply --unidiff-zero scripts/release/README-v0.2.0.patch
git diff --check
git diff -- README.md
bash test/run.sh
git add README.md
git commit -m "Point beginner downloads to verified v0.2.0 prerelease"
git -c 'credential.helper=!gh auth git-credential' push -u origin release/v0.2.0-download-links
gh pr create --base main --head release/v0.2.0-download-links --title "Update verified v0.2.0 download link" --body "Point the beginner guide to the publicly downloaded, checksum-verified and compiled Arduino software prerelease. Hardware qualification remains pending."
~~~

Review and merge that link-only PR through normal checks. Then update About:

~~~sh
gh repo edit iamaritrasaha/REVIX-LINK --homepage https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.2.0
gh api repos/iamaritrasaha/REVIX-LINK --jq .homepage
curl --fail --location --head https://github.com/iamaritrasaha/REVIX-LINK/releases/tag/v0.2.0
~~~

Keep the v0.1.0 release, tag and assets untouched. The completed software
publication still does not qualify motorcycle RPM integration, transceiver
electrical passivity or the actual strip's direct 3.3V signal compatibility.
