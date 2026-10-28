# Release checklist

- Use Ubuntu WSL and a clean, synchronized main branch.
- Pass [software verification](test/VERIFICATION.md), all four firmware builds and all 15 Arduino configurations.
- Validate the versioned ZIP, checksum and byte-for-byte reproducibility.
- Require successful CI on the exact candidate commit.
- Keep DEMO as default and the NS125 profile UNVERIFIED.
- Obtain owner approval of the exact commit and ZIP SHA-256 before creating a tag.
- Follow [the release procedure](RELEASE_PROCEDURE.md); never overwrite existing tags or assets.
- Verify public downloads and an extracted Arduino compile before updating the About homepage.
- Preserve COPYRIGHT, receive-only restrictions and all hardware qualification limits.
