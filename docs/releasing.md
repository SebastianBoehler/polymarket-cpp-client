# Releasing

Publication requires the maintainer's authorization. If publication after checks
is already authorized, complete these steps without asking again.

1. Inspect the latest release, tags, and commits on `main`. Choose the version
   from the public API changes: patch for compatible fixes, minor for compatible
   additions, major for required migrations. Assess binary compatibility
   separately and tell consumers when they must rebuild.
2. Update `CMakeLists.txt`, all version macros in
   `include/polymarket/version.hpp`, and README installation examples together.
   Add curated notes at `docs/releases/<tag>.md`, for example `v3.0.0.md`.
3. Include all changes since the last release. Name new APIs, fixes, compatibility
   concerns, wallet and approval requirements, and verification limits.
4. Build examples and tests, and run the offline suite including
   `test_package_consumer`. Open a PR and wait for every Linux/macOS Debug/Release
   build and the PR title check to pass on the final commit.
5. Merge the verified PR. Confirm its commits are on `main` and that the exact
   commit to tag passed the complete build matrix. Preserve useful commit groups
   with a merge or rebase when available.
6. Create an annotated `vX.Y.Z` tag at that commit and push the tag. Existing
   tags are immutable; do not replace a published tag to fix a failed release.
7. Monitor `.github/workflows/release.yml`. It validates tag/version agreement
   and the notes file, rebuilds and tests Linux x86-64 and macOS arm64 packages,
   then publishes only after both package jobs pass. A failed job is a blocker.
8. Verify the published tag resolves to the intended commit, the curated notes
   are present, and both package assets exist. Download the packages to inspect
   headers, archives, CMake exports, and licenses. Build a consumer from the
   downloaded archive on each available matching platform. State which
   validation ran locally and which ran in CI.

The release workflow is the source of truth for supported runners, deployment
targets, package names, and action versions. Keep it and these steps consistent
when changing release behavior. A release is complete only when the published
assets and notes have been verified.
