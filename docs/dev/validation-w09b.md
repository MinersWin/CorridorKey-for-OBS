<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Production catalogue and activation validation

The Windows x64 build used the OBS 30.2 SDK, ONNX Runtime 1.31.1 headers and
RelWithDebInfo. Both `CORRIDORKEY_CK2_DEV` and `CORRIDORKEY_MANAGER_FIXTURES` were
off. The host was a fresh portable OBS 33.0.0 under
`S:\Projects\obs-test\obs-33.0.0-w09b`, with an initially empty plugin config.
The installed OBS and other portable test instances were left alone.

## Catalogue verification

Graph revision `5d88114f169cb082cda238426af47feffb9724af` was queried through the
anonymous Hugging Face API. Every released ONNX and JSON file was downloaded and
checked against the revision 3 SHA-256 list and API byte counts. The production
packages use separate MoGe, RVM and mask-free stages. Quality includes Stage 2.
The optional merged stage is not selected.

Checkpoint revisions, LFS SHA-256 values and sizes came from the anonymous API.
Local CK2 and foundation-stat copies independently matched those pins. The real
RVM download matched both SHA-256 and DeepMind's published MD5. All 18 available
local exporter recipes produced byte-identical bound weights through the native
binder. See the [complete inventory](catalogue-v1-inventory.md) and
[machine-readable evidence](validation-w09b.json).

## Real first run

The GUI test added CorridorKey to Mixkit's reporter clip and opened the production
manager. Set up CorridorKey selected Balanced. The download button stayed disabled
until the displayed model licences were accepted. Native HTTPS downloaded all 12
required files, 3,296,058,594 bytes in total, anonymously from their pinned URLs.
There was no local preseed or offline import for this transfer. The native binder
then prepared all four graph outputs and wrote verified ready and selection
receipts. Screenshots and instructions are in the [first-run guide](../user/first-run.md).

This test exposed a source-gap reset during backend preparation: passthrough frames
were not updating the last-render timestamp. The fix records those frames and
retains the verified model's reader lease across source resets. The corrected build
produced a live Balanced key through the normal cache path with DirectML. Selecting
an uninstalled Facecam package returned to the original video with a clear status;
returning to Balanced resumed keying.

A second install used a fresh model state with only the 12 verified source blobs
copied from the first transfer. It repeated licence acceptance and native binding
to check activation after installation without restarting OBS. Original network
evidence and receipts remain in the isolated portable copy's `models-first-network`
directory. The repeated run did not claim a second network transfer.
It reached DirectML and published keyed frames after binding without a restart;
all four bound outputs were independently hashed again.

Windows displayed a firewall prompt; the owner dismissed it with Cancel.
CorridorKey needed no inbound access. Captures use the actual OBS windows with the
cursor outside each captured dialog. They contain no fixture data or edited UI.
The GPU was shared with another workload, so the captures establish behavior and
make no performance claim.

The firewall overlay returned on later launches. Desktop input was rejected
because `PickerHost.exe` owned the overlay, including clicks on OBS outside the
prompt. Background installation and activation continued. The affected test
instance was stopped by its verified process ID after its package and live-frame
evidence had been saved. Other OBS instances were left running.

## Backend preparation visibility

The manager's task registry receives the actual W06 phase callbacks and exposes
an indeterminate bar, elapsed time and cancellation. Tests cover transitions,
filter lifetime, cancellation and cleanup after preparation failures. During the
fresh-state install, accessibility reported Testing backend with elapsed time and
an indeterminate progress value, then Ready. The firewall overlay prevented a
clean capture of that phase.

A separate run provisioned the existing W19 NVIDIA bundle locally and restarted
the portable host. It did not repeat the runtime installer's licence/download
walkthrough. Its probes exposed Windows delayed-library lookup failures in the
TensorRT builder resources and cuDNN components. The loader now preloads the
verified components by full private path. Tests reject a bad hash and unlisted
DLLs, and confirm that the listed components are loaded from the bundle.

After that fix, the real standalone TensorRT FP16 probe passed on the RTX 5070 Ti,
with C API 31, ORT 1.31.1 and GPU partition verification. This validates its
standard-op fixture; it does not establish CK2 TensorRT model execution. The
overlay prevented opening the manager and testing Cancel task, so a real TensorRT
build screenshot and UI cancellation remain unverified. The first-run images
cover the passing DirectML workflow. Optional NVIDIA test files were retained
outside the portable host's active bundle-discovery directory.

## Checks and limits

All 15 applicable CTest suites passed, including the local binder parity suite.
The final 14-suite rerun excluded that already completed parity run and passed.
The settings OBS test required the OBS runtime DLL directory on PATH. Clang-format
19 checked every tracked C/C++ file; gersemi 0.22.2 checked every CMake file.
Actionlint checked the release workflow, and `git diff --check` passed.

The release still needs the coordinator's catalogue key, public key integration
and signing step. Production draft creation fails until that step is wired.
Development bundles admit only the exact catalogue bytes pinned in the executable
from installed module data; downloaded manifests and artifacts remain SHA-256
pinned. See the [trust model](models-catalogue.md). The reserved
`model-catalogue-v1` manifest URLs must be published with the release.

Linux and AMD inference were not exercised on this machine. The original W09b
checks used reconstruction without learned refinement. The rebased
pipeline also activates W13 from the production refiner package. Existing
Quality reference parity has a baton-interface mismatch in the W09 evidence and
needs a separate parity update. This work does not claim new numerical model
parity from the live screenshot.

## W13 rebase verification

The five activation commits were rebased onto `cf72745`. Production activation
leases the CK2 package and, when enabled, the separate refiner package. The tile
budget selects `refiner_b<N>`. The prepared-pipeline identity includes both
package digests, refiner enablement and tile budget. Refiner handoff and worker
retirement are enabled in normal runtime builds. The explicit environment
variable override remains restricted to development builds.

Both Windows x64 SDK builds (OBS 30.2 baseline and OBS 33 current) passed with
ORT enabled, `CORRIDORKEY_CK2_DEV=OFF` and compiler warnings treated as errors.
All 15 applicable CTest suites passed for each SDK, including the local binder
suite with all 18 available CK2/refiner recipes byte-equal to the exporter.
CK1 recipes were excluded from the local fixture because their checkpoints
were absent. Clang-format 19.1.5 checked every tracked C/C++ file, gersemi 0.22.2
checked every tracked CMake file, and Git whitespace checks passed.

The portable OBS 33 test used a separate collection containing only a looping
1080p media source and a scene. The CK2 cache was reused; the refiner package was
preseeded from hash-verified release files with a local smoke-test receipt.
DirectML loaded the production `refiner_b512` graph and ran W13's native tile
atlas. Scripted setting changes exercised refinement off, on and off again.
The diagnostic 1000 ms stale timeout allowed refined publication, and native
composed-output captures showed a visible key with refinement enabled. The default
250 ms timeout also reproduced W13's documented late-frame rejection under
DirectML; it remains unchanged in the plugin. These checks establish activation
and runtime integration, not new model parity or real-time performance.

OBS websocket remained disabled (`server_enabled: false`). A Windows Firewall
prompt still appeared despite the media-only collection; the OBS process had
no listening TCP or bound UDP sockets when checked. The prompt was left alone.
Native OBS screenshot APIs were used for output inspection; an unobscured
properties-window screenshot remains pending. The final host exited normally
and reported zero memory leaks; the smoke-test script was detached afterwards.
