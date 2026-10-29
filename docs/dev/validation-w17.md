<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W17 diagnostics and compatibility validation

29 October 2026. Windows 11 x64, MSVC 19.44.35229, Windows SDK 10.0.26100 and
the shared VS 2022 Build Tools. The implementation is commit `f8ae5e5` on
`w17-diagnostics`. The diagnostics capture identifies that commit with `-dirty`
because the support documentation was still being edited at configure time.

## Builds and tests

| Build | Result |
| --- | --- |
| OBS 30.2 / Qt 6.6.3 headers, diagnostic fake and capture fixtures enabled | Plugin, helper and tests compiled with compiler warnings treated as errors. |
| Same build, ORT 1.31.1 headers enabled | Native helper and mock runtime compiled; full CTest run passed. No optional vendor runtime is required to load the plugin. |
| OBS 33 / Qt 6.11.1 headers, default release configuration, fixtures/fake disabled | Plugin compiled with compiler warnings treated as errors. |

CTest completed all 12 entries: 11 passed and the local-only model-weight parity
entry skipped. `CK_BINDER_EXPORTS` pointed at an empty build-local directory,
matching CI's no-checkpoint environment. No weights were rebound for this package.
The seven diagnostics cases passed 90 assertions. They cover:

- OBS 30.2/31/32/33 and a simulated newer host, with no rejection based on the
  unfamiliar version text.
- Missing optional FPS export, configured rational-FPS fallback and invalid
  live FPS values.
- Missing required registration export: load refusal names the missing symbol
  and tells the user to install a compatible host/plugin.
- Adapter mismatch, a helper returning another provider, permitted GPU fallback
  and passthrough when no candidate succeeds; CPU never becomes a live fallback.
- Graph/manifest hashes, runtime bundle/core/API identity, W15 timing values,
  nonfinite timing, recognized settings and excluded free text.
- Windows/Linux/macOS home paths, account names containing spaces, credential
  patterns and bearer/authorization assignments.
- Error codes with static actions, discarded helper/error text and removal of
  per-filter callbacks before teardown.

W06's existing mock-runtime tests also negotiate an older API under a `999.0.0`
version string and reject a conflicting core. The real-CPU integration test is
opt-in and was not run. No GPU job is part of the policy unit tests. The new
diagnostics target runs automatically in the existing Windows/Linux Contracts
workflow; no hosted CI run is claimed before the coordinator pushes the branch.

The first local CTest attempt found an unset OBS DLL search path and Python 3.9,
which lacks the existing parity script's `hashlib.file_digest`. The successful
run used Python 3.12.15 and the installed OBS `bin/64bit` on PATH. These were
local test setup corrections; neither existing test was disabled or rewritten.

Import-table inspection found no ONNX Runtime, CUDA, TensorRT, cuDNN or DirectML
DLL dependency in the plugin. clang-format 19 dry-run with `--Werror`, gersemi
0.22.2 across all tracked CMake files, Git whitespace checks, issue-form YAML and
PowerShell parsing passed. Documentation and form help text received the required
humanizer review. GitHub reported Discussions disabled, so the chooser offers a
documentation contact link without a Discussions link.

CMake still reports doctest's existing minimum-version deprecation. MSBuild
still reports MSB8074 for the two existing `cache.cpp` basenames. Neither is a
new C++ compiler warning; compilation and linking passed.

## Linux compilation scope

Zig 0.15.2's Clang compiled Linux x86-64 objects with C++20,
`-Wall -Wextra -Werror` for the diagnostics collector, host policy, diagnostics
tests, device enumeration, disposable probe, runtime loader, ORT session and
per-graph preparation. ORT headers report API 31 from the existing 1.31.1 source
archive. These checks include the plugin-EP/MIGraphX session-selection code.

This is build-only evidence for those objects. No MIGraphX vendor binary was
linked or loaded. No AMD GPU, full Linux plugin link, GCC run, Linux Qt/sysfs
inventory run or Linux OBS session was available. Ubuntu remains an expected
host target; the matrix does not turn object compilation into a hardware claim.

Two W06 warning fixes were necessary for these checks: guard the Windows-only
LUID helper and initialize every `GraphAttempt` member before assigning the
backend/precision. W06's selector also now rejects a result naming another EP
and continues through its permitted candidates. No shared C++ contract changed.

## Real OBS check

The installed-host lock belonged to W09. Testing used an isolated portable OBS
33.0.0 copy under `S:/Projects/obs-test/obs-33.0.0-w17`, cloned from the installed
application. The script claimed its own portable lock, used CorridorKey Test
and launched only its own process. It did not modify installed OBS, its plugin,
the owner's collection or another package's session.

The production manager opened the production diagnostics preview. It collected
the real OS/kernel, Qt, D3D11 renderer, FPS, GPU drivers/LUIDs and cache state.
DXGI exposed two RTX 5070 Ti adapter entries with different LUIDs; both are kept
in the report. The DXGI driver version is `32.0.16.1692` (`nvidia-smi` reports
616.92). No inference bundle or installed model was present in this profile;
availability and model states report that absence.

A private 1280 × 720 color card on a 1920 × 1080, 30 Hz canvas exercised the
fake filter with a configured 20 ms delay. W15 provided non-null age, transfer,
inference, queue and processed-FPS values. These are fake-pipeline observations
and do not establish real-model speed.

The fixture verified that scene/source/audio-name markers were absent, pressed
Copy to clipboard, compared the result with the preview and restored the prior
clipboard text. It saved the exact report and cursor-free widget screenshots.
The entire report and both captures were reviewed by eye for personal paths,
credentials, source names and readability. A separate scan checked the local
account name, common home-path forms and token prefixes. No leak was found.

The first capture attempt exposed an unsupported fake-backend name in the
report builder; the collector now handles it and a regression assertion covers
it. A later capture succeeded but the script's original 45-second process
deadline expired; the host's log recorded normal teardown and zero leaks.
The final 90-second run completed the preview/clipboard/privacy checks and normal
shutdown, reporting zero leaks. The private scene and temporary plugin were
removed and the portable lock released.

See [the screenshots and reviewed report](../images/diagnostics/README.md).
Raw OBS logs stay in the ignored worktree cache; no log was uploaded or committed.

## Integration seams and remaining checks

W17 adds its own CMake fragment; the root change only appends `w17` to the package
list. Filter changes register/remove snapshot readers and record observed error
codes. W07's preview/report actions use the typed collector; the old portable
report entry point delegates to it. Backend-test completion records a sanitized
probe result. W09 should record actual preparation results at that same seam as
its pipeline integration lands.

OBS 31/32, Windows 10, AMD/Intel hardware, native GPU video transports and a full
neural-model run remain unqualified here. Descriptor hashes are reported without
rehashing libraries during preview. Model verification comes from the manager's
Ready state; the live capture had no installed model, so tests cover those hash
fields. Error history records observed code changes, not every transient error.
Optional source-name sharing and Save diagnostics to file are not implemented.
Nothing uploads automatically.

A mock missing required export demonstrates the plugin's clear refusal message.
An ABI broken so early that the OS loader cannot resolve imports can still fail
before the module's own checks execute. No deliberately incompatible host binary
was loaded into OBS. Older/newer host and driver ranges remain capability-based.
