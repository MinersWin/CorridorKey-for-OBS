<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Model-manager integration

`src/ui/manager_model.*` owns presentation state and action eligibility without
Qt or OBS dependencies. `ManagerController` owns W04 services, cancellation
sources and a bounded command queue. Its worker performs cache scans, receipt
checks, initial package verification and removal. W04 owns the transfer worker.
Qt polls snapshots every 500 ms; rendering never accesses the cache.

`ModelManager` uses public OBS frontend entry points and Qt widgets. Tools and
properties share one window and one service. The property callback retains no
filter pointer. Closing the manager hides it; service tasks remain alive.
Shutdown cancels workers and destroys UI before Qt exits. Licence, diagnostics,
removal and file-selection windows are modeless. Backend tests refuse to start
while either streaming or recording is active.

## Release catalogue

The bundled production index uses the installer-only compiled-digest admission
path while the coordinator provisions the release key. Remote catalogues always
require signatures. See [catalogue trust](models-catalogue.md) for the boundary
and release requirements. With `CORRIDORKEY_CATALOGUE_PUBLIC_KEY` set, the
installed catalogue must also have a valid P-256 signature.

The installed data tree then needs:

```text
models/catalogue-v1.json
models/catalogue-signature.json       {"signature": "<128 hex digits>"}
models/manifests/<manifest-sha256>.json
models/licenses-v1.json
models/sources-v1.json
```

W04 authenticates the catalogue, persists its sequence floor, checks the pinned
manifest digests and validates the exact licence hashes. These files supply
metadata only. Payload transfers still use the publisher URLs from the admitted
manifests. Metadata refresh is a release/catalogue integration task; the manager
does not fetch arbitrary unsigned manifests from the network or a ZIP.

## W05 binding seam

Register `set_model_binder(Binder)` on the UI/control thread before the manager
first opens. Registration returns false once its service exists, so replacing a
binder cannot invalidate an active install.

```cpp
using Binder = std::function<BindingResult(
    const models::Manifest &, models::Cache &, std::stop_token,
    const std::function<void(BindingProgress)> &)>;
```

W04 prepares the staged graph/JSON files before calling this adapter. The binder
reads authenticated cache blobs, writes only the declared staged weights, checks
cancellation at safe boundaries, and reports completed/total bytes. Zero total
means unknown. Return `BindingResult{error(...)}` on failure. W04 verifies every
output before publishing a revision. The adapter must not publish or select it.

With no adapter registered, the controller calls W05's native `bind_package` and
forwards its byte progress. Synthetic tests exercise both an explicit adapter and
the native safetensors path. The first-run walkthrough uses the real CK2 sources.

## Backend helper and later integration

Windows resolves `data/tools/corridorkey_backend_probe.exe`. Linux uses the
configured install libexec directory. W19 supplies generated standard-op graphs
as `data/tools/probe-fp32.onnx` and `probe-fp16.onnx`, plus a trusted installer
index at `data/runtimes/probes.json`:

```json
{"schema_version": 1, "bundles": {"directml": "windows-x64/dml/directml.json"}}
```

Descriptor paths are relative to `runtimes/` and pass W04 path validation. W06
loads and validates their runtime definitions. If a bundle is missing, the test
reports that provider as unavailable. Tests use an empty configuration to check
that path. W07 does not distribute runtime DLLs or benchmark camera frames.

The filter worker selects a package by grid and whether the preset requires
Stage 2. It checks the acceptance receipt and acquires W04's verified reader
lease before preparing ORT sessions. A successful preparation selects the
revision explicitly; installation alone leaves `current` unchanged. Missing
packages keep video in passthrough and are detected after installation without
recreating the filter. Unsupported grids require a supported package/preset.
Installed revisions absent from the catalogue remain available for licence
review and safe removal, but cannot start new downloads.

W06's actual graph-preparation phases feed the manager through a thread-safe
per-filter task registry. TensorRT helper work shows Building TensorRT engine;
probes and in-process session loading show Testing backend. Both use an
indeterminate bar and elapsed time. Cancel requests the filter's stop source;
the helper exits cooperatively and in-process creation returns at its next safe
boundary. Closing the manager leaves preparation running. Filter teardown removes
its registry entry. The UI never waits for compilation.

Copy diagnostics uses [W17's typed collector](diagnostics.md) and a modeless
preview. Raw helper JSON, user-selected import paths, source names, arbitrary
errors and OBS logs stay outside the report. Report a problem opens the fixed
GitHub issue chooser; it sends no diagnostics. The tests check that local error
text, credentials and personal paths cannot enter copied fields.

## Reproducing validation

Build with `CORRIDORKEY_MANAGER_FIXTURES=OFF` for release. Portable tests need no
Qt or OBS. CTest generates its tiny offline bundle with W04's test executable:

```powershell
cmake -S . -B build-contracts -DCORRIDORKEY_BUILD_PLUGIN=OFF
cmake --build build-contracts --config RelWithDebInfo
ctest --test-dir build-contracts -C RelWithDebInfo --output-on-failure
```

For OBS captures, build the plugin against the 30.2 SDK with
`CORRIDORKEY_MANAGER_FIXTURES=ON`, run CTest once to generate `manager-fixture`,
then run `tests/ui/run-obs-captures.ps1`. It checks for an existing OBS process,
takes the shared installed-OBS lock, uses the CorridorKey Test collection, and
restores the previous plugin and preferences after each capture series.
Pass `-PortableRoot S:\Projects\obs-test\obs-33.0.0-w07` to use the isolated OBS
33 copy while the installed host is locked. This mode changes only the selected
portable host and leaves the installed-OBS lock alone. `-Scale 0.5` and
`-Scale 0.75` produced 100% and 150% captures on the test machine's 200% desktop;
check the recorded device-pixel ratio when using a different desktop scale.
`run-obs30-smoke.ps1` uses the dedicated portable 30.2 installation and restores
its plugin and preferences. Both launch only their own OBS process and request
normal shutdown. Neither script may interrupt an existing OBS session.

The capture code is absent from release builds. It drives real Qt widgets
inside OBS and captures their rendered pixels without a mouse cursor. State
previews inject labelled snapshots. A separate accepted folder import runs
through W04 and the tiny binder after the manager is hidden. Backend absence,
the unchecked acceptance gate and shared Tools/properties instance are checked
inside the host. See [validation](validation-w07.md) for results and limits.
