<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Release checklist

## Prepare v0.1.0-rc.1

1. Merge the reviewed release changes and any separately reviewed catalogue update.
   Keep `buildspec.json` at `0.1.0`; the RC suffix belongs to the Git tag. CMake's
   plugin version, diagnostics, Windows version resource and installer version
   derive from that value. Do not hard-code a second version in the installer.
2. Review [licensing and provenance](release-audit.md), [compatibility](../compatibility.md),
   the [changelog](../../CHANGELOG.md), and `packaging/release-notes.md` against the
   exact commit. Recheck licence hashes and screenshots after catalogue changes.
3. Run the CI checks: clang-format 19.1.7, gersemi 0.22.2, REUSE lint, Windows and
   Linux contract tests, graph-fixture generation, baseline/current plugin builds,
   catalogue-signing tests and packaging inventory tests. Build release binaries
   with UI fixtures and development overrides disabled.
4. Confirm `packaging/catalogue-public-key.txt` matches the protected environment's
   signing key. The private `CATALOGUE_SIGNING_KEY` belongs only in the `release`
   environment. Do not print it, include it in artifacts, or rotate it as part of
   routine release work. Retain the owner's offline backup.
5. After the release owner approves the commit, create and push the tag:

   ```sh
   git tag -a v0.1.0-rc.1 -m "CorridorKey 0.1.0 release candidate 1"
   git push origin v0.1.0-rc.1
   ```

   Tag creation and pushing are release-owner actions. This checklist does not
   create a tag. A branch workflow dispatch produces unsigned test artifacts;
   `v*-test.N` tags cannot produce a release draft.
6. Watch the Release workflow. Approve its `release` environment after reviewing
   the tag and workflow commit. It signs the exact catalogue bytes, verifies the
   staged signature/public key/digest, assembles packages and creates a draft
   prerelease. The draft job also uses the protected environment.

## Verify the draft

- Download the Windows installer, modern and legacy zips, both Ubuntu deb/tar.xz
  lanes, `BUNDLE_CONTENTS-*`, `THIRD_PARTY_NOTICES-*` and `SHA256SUMS.txt`.
  Recompute every asset hash. Compare the list against the draft and reject
  missing, duplicate or `unsigned-test-` assets. RC asset filenames use `0.1.0`;
  the release tag distinguishes the candidate.
- Inspect the packages for one plugin layout, the native helpers, full vendor
  notices and the signed model catalogue. Verify no private key, model weights,
  CorridorKey-derived graphs, compiled engines, Python environment or local paths
  leaked into them. The two synthetic Add probe graphs are intentional.
- Install with OBS closed on Windows OBS 30.2 and 33+, and on each supported
  native Ubuntu lane. Test both portable zip layouts separately. Confirm version
  `0.1.0`, one loaded module, clean-cache first run, licence refusal, accepted
  download, cancellation/retry, offline import and activation.
- Smoke-test the actual release package with CK2 presets, learned refinement,
  experimental CK1, two sources, changing source sizes, missing-runtime
  passthrough, diagnostics redaction and audio-offset restoration. Use a baseline
  clap recording for lip-sync. Record hardware, runtime and graph revisions.
- Test upgrade, rollback and uninstall. Verify refusal while OBS is running,
  duplicate-module handling, and the choice to retain or delete the default cache.
  Use the [packaging validation procedure](packaging.md) and keep logs.
- Review every workflow result and unresolved finding. Linux build success alone
  does not qualify a Linux live-host install. No local rebuild substitutes for
  installing the downloaded draft assets.

## Publish

Replace the draft text with the reviewed `packaging/release-notes.md` and attach
any missing evidence. Keep **Set as a pre-release** checked for `v0.1.0-rc.1`.
Publish only after the release owner accepts the recorded limitations and the
draft smoke tests pass. Do not move a published tag to repair an artifact; cut
the next candidate instead.

## Limitations to retain in release notes

- Linux GPU interop is not implemented; native Linux runtime validation is limited.
- AMD and Intel DirectML need community testing. No qualifying report is recorded
  yet, so do not label them hardware-tested or community-tested without a link.
- CK1 is experimental and has worse edges than CK2 in the evaluated clips.
- Multi-instance testing found a positive memory trend; its cause is unresolved.
- Windows binaries have no Authenticode code signature and may trigger SmartScreen.
  Model-catalogue signing is separate from Windows code signing.
- Flatpak and HDR input are unsupported. First TensorRT preparation can take
  minutes, and the DirectML/NVIDIA runtime-core switch requires restarting OBS.
