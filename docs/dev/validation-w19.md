<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W19 validation, 29 October 2026

The worktree is `w19-packaging`, based on `172e667`. Builds, vendor archives and
release artifacts remain in ignored directories. No push, tag or release was
created. No signing key was generated, read or imported.

## Builds and checks

Windows 11 x64, MSVC 19.44.35229, Windows SDK 10.0.26100, CMake 3.31.10 and
Inno Setup 6.5.4 were used. The release build uses OBS 30.2.0 / Qt 6.6.3; the
current-SDK lane uses OBS 33.0.0 / Qt 6.11.1. Both ORT-enabled plugin/helper/CLI
builds passed with compiler warnings treated as errors. MSBuild emitted its
existing duplicate-`cache.cpp` module-dependency tracker warning; compilation
and linking succeeded.

All 13 enabled CTest cases passed on each SDK lane. The local-model parity case
was excluded; this task did not run neural model inference. The tests cover
W04's HTTP resume/verification and acceptance behavior, the existing runtime
supervisor, and the new packaging cases: runtime trust-anchor rejection,
disabled bundle rejection, acceptance before installation, exact ZIP-member
selection, large deflate streams, CRC/size corruption and duplicate members,
model exclusion, stable inventories and both Windows archive layouts.

clang-format 19 dry-run, gersemi 0.22.2 across the CMake files, actionlint 1.7.12,
Python syntax and Git whitespace checks passed. Actual modern/legacy zip
inventories were checked against every recorded payload hash, and release
checksums matched. Only the generated inventory and generated notice omit their
own hashes.

Linux default-core extraction and ELF inspection ran on Windows using Python.
The base runtime is 29,685,537 bytes including descriptors/notices. The GPU
recipes include all observed non-system `DT_NEEDED` dependencies, including
cuRAND; inspected core/cuBLAS/cuDNN/TensorRT libraries retain `$ORIGIN` search
paths. NVRTC and nvJitLink are included for runtime compilation and preloaded
by full path. This is static inspection, not a Linux build or execution result.
WSL/Linux was unavailable. Ubuntu 24.04/26.04 `.deb`/tar creation and installation
remain pending hosted CI/native validation.

## Windows installation evidence

The official OBS 33.0.0 x64 archive was verified against GitHub's asset digest
`3369a3ecd67b5669a8a08d77357f87cc786d1fba42fbbd1054d841a69f6271a7`.
It was extracted to `S:/Projects/obs-test/obs-33.0.0-w19`, with portable mode.
The installed OBS and other packages' portable copies were not modified or closed.

- A 0.0.9 installer fixture installed successfully into the portable `plugins`
  tree. This fixture represents a prior installer version, not a historical
  public release.
- The 0.1.0 upgrade returned exit code 0, appended to the existing uninstall log,
  removed a seeded `bin/64bit/corridorkey.dll`, and retained the default cache
  sentinel. The modern module remained at `plugins/corridorkey/corridorkey.dll`.
- OBS logged one CorridorKey load, compiled against API 30.2, and an enabled
  CorridorKey filter on the test color source. The upgraded runtime-manager
  Tools entry opened, showed the full licence texts and left downloading
  disabled before acceptance.
- A running OBS caused the installer to refuse before installation. Its log
  records "Close OBS Studio before installing CorridorKey." No OBS process was
  terminated by the installer.
- The test instances closed normally after UI checks. OBS reported one
  outstanding allocation, also present in the foundation's earlier OBS 33
  evidence. Its ownership was not established here. One earlier hidden launch
  was stopped before relaunching visibly; its subsequent crash-recovery prompt
  was dismissed only for this dedicated test copy.
- Uninstall and rollback remain pending. Another package needs its OBS instance;
  the coordinator requested that W19 stop waiting and hand over a noninteractive
  smoke script. The interactive uninstaller was cancelled before confirming any
  removal. The script's syntax, read-only stage/path checks and refusal while
  OBS is active passed; its actual install/uninstall sequence has not run.

Evidence stays in `.cache/install-previous.log`, `.cache/upgrade.log`,
`.cache/blocked-final.log`, the portable OBS log directory, and the images in
`docs/images/install`. Screenshot metadata records the exact fixture/revision;
the pointer is outside each capture and no UI was retouched.

## Optional runtime assembly

The conventional Windows TensorRT/CUDA bundle assembled from verified archives.
The final RTX bundle also assembled successfully through the native CLI using
W04's acceptance receipt and verified blob cache. No Python is needed by the
installed runtime flow. These checks used vendor archives already downloaded
for the licence audit; W04's HTTP tests cover the transport independently.
The RTX SDK's runtime and parser DLLs matched the EP wheel byte for byte, and
the assembled bundle includes the SDK acknowledgements, the full SLA and EP
licence text. See [licence evidence](packaging.md).

No new GPU performance or full-graph probe result is claimed. The shared GPU
was busy with other work (one check showed 45% use and about 14 GB allocated).
Runtime installation/hash checks ran without starting inference. W06's prior
probe results remain separate evidence; a model needs its own preparation and
parity checks.

| Artifact or recipe | Bytes |
| --- | ---: |
| Windows installer, unsigned | 14,981,085 |
| Windows modern zip | 16,668,073 |
| Windows legacy zip | 16,668,093 |
| Windows ORT/DirectML base runtime tree | 36,227,184 |
| Windows RTX vendor downloads | 522,539,545 |
| Windows RTX selected native/licence files | 304,164,147, plus generated metadata/notices |
| Windows TensorRT/CUDA vendor downloads | 3,142,927,147 |
| Windows TensorRT/CUDA selected files | 3,901,058,458, plus generated metadata/notices |
| Linux TensorRT/CUDA vendor downloads | 5,086,872,244 |
| Linux TensorRT/CUDA selected files | 4,785,240,422, plus generated metadata/notices |

The package helper enforces GitHub's limit below 2 GiB per release asset. NVIDIA
archives are never uploaded to the project's releases or Actions artifacts.

## Integration and remaining release work

### Deferred uninstall/rollback script

From the W19 worktree, in an OBS-free window, run:

```powershell
Set-Location 'S:\Projects\ck-obs-wt\w19-packaging'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\packaging\smoke-uninstall.ps1 -OBSRoot 'S:\Projects\obs-test\obs-33.0.0-w19'
```

The execution-policy flag applies only to that PowerShell process. The script
uses the already prepared `stage/corridorkey` and `.cache/innosetup/ISCC.exe`.
It verifies the stage inventory, compiles fixtures with a separate uninstall
identity, removes an existing recognised fixture, installs the previous-version
fixture, upgrades it, checks alternate-layout removal, silently uninstalls,
rolls back and uninstalls again. It checks module hashes, installer versions and
cache/config sentinels at each step. Each completed step prints `PASS`; a failure
prints `FAIL` and stops. Evidence remains under `.cache/packaging-smoke/<id>`.

The default prior-version fixture uses the current payload with version 0.0.9;
it tests installer mechanics. Pass `-PreviousStageDir <verified-prior-stage>` to
test different prior binaries. `-StageDir`, `-InnoCompiler`, `-Version` and
`-PreviousVersion` override the other inputs. `-PreflightOnly` performs the
read-only checks. All variants refuse while any OBS instance is active.
The script leaves the portable plugin uninstalled on success and retains
existing user configuration/cache; it removes only its own sentinel files.
It does not exercise the interactive cache-deletion choice or a new OBS load.

### Release follow-up

The root CMake list gained only `w19`; `src/plugin-main.cpp` gained the runtime
manager include and register/shutdown hooks. W04/W06/W07 implementation files and
shared contracts were not changed. The existing runtime-bundles document links
to the release recipes while retaining its historical W06 evidence.

The coordinator still needs to run the smoke script with OBS closed, run the
public CI Linux lanes, and validate the packaged runtimes against actual models
through W09. AMD/MIGraphX remains unavailable until its dependencies and hardware
are validated. Cross-core DML/NVIDIA fallback requires restart; no unified core
was built. Catalogue signing and Windows code signing remain owner work. The
workflow's catalogue placeholder references the protected environment secret
without implementing a signer.
