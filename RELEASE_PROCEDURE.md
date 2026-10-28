# Software prerelease procedure

Run development and release commands inside Ubuntu WSL.
Use a new, unused v0.x.y version. Never move existing tags or replace published assets.

## Before publication

1. Merge the preparation PR, synchronize main with a fast-forward pull and confirm a clean tree.
2. Match RELEASE_NOTES.md to the new version. Keep its software-prerelease and hardware qualifications.
3. Run [verification](test/VERIFICATION.md), all four PlatformIO targets and all 15 extracted Arduino configurations.
4. Build the versioned Arduino ZIP twice. Validate both packages, their checksums and identical bytes.
5. Confirm successful GitHub Actions on the exact main commit.
6. Record the full commit SHA, CI URL, ZIP size and SHA-256 locally.

**Stop before tag creation. The owner must approve the exact commit and ZIP checksum.**
If main or the package changes, repeat verification and obtain fresh approval.

## After approval

1. Recheck the clean tree, approved local/remote main SHA, checksum and absence of the target tag/release.
2. Create an annotated tag at the approved commit and push only that tag. Never force-push.
3. Wait for release.yml to pass its notes, full verification and publication jobs.
4. If any required job fails, stop and preserve logs. Do not publish manually or replace the tag.
5. Verify a nondraft prerelease, exact tag commit, approved notes and expected ZIP/checksum assets.
6. Download both public assets, verify checksums and compare bytes with the approved local build.
7. Validate and compile the extracted public Arduino sketch.
8. Check README download guidance, then update and reread the GitHub About homepage.

Keep detailed approval records and logs local. Published software does not qualify
motorcycle RPM mapping, CAN electrical passivity or direct 3.3V LED compatibility.
