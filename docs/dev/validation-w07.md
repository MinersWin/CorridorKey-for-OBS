<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W07 validation, 29 October 2026

## Build and unit tests

Windows 11 x64, MSVC 19.44.35229 and the shared VS 2022 Build Tools. Both builds
used compiler warnings as errors:

| SDK | Qt headers | Result |
| --- | --- | --- |
| OBS 30.2.0 baseline | 6.6.3 | Plugin and tests passed; capture fixtures enabled |
| OBS 33.0.0 current | 6.11.1 | Plugin and tests passed; capture fixtures disabled |

All eight CTest entries passed in each build. The manager's eight test cases
passed 89 assertions. They cover action eligibility for every state, safe
cancellation, unknown totals, transfer-rate resets, first-run detection, source
sizes, diagnostics redaction, receipt refusal, accepted offline import, reader
leases, retry, missing binder failure, and retained revisions after a catalogue
update. The existing W04 HTTP, signature and package tests also passed.

Import-table inspection of `corridorkey_manager_tests.exe` found no OBS or Qt
dependency. Its presentation model and controller link to the portable core.
No Linux build was run locally.

clang-format 19 dry-run with `--Werror` passed across C++ sources and headers.
gersemi 0.22.2 passed across all CMake files. PowerShell syntax checks and Git
whitespace checks passed. The CMake configure step reported the existing
doctest minimum-version warning. The current-SDK build also reported MSBuild
MSB8074 for the existing pair of `cache.cpp` source basenames; compilation and
linking succeeded.

## OBS hosts and isolation

The plugin was compiled against the 30.2 API for all host tests. Each run used
the dedicated CorridorKey Test scene collection and tiny independently authored
files. No CorridorKey checkpoint or large download was used.

| Host | Qt runtime | Checks | Shutdown |
| --- | --- | --- | --- |
| Official portable OBS 30.2.0 | 6.6.3 | Shared Tools/properties window, unchecked licence button, missing backend, accepted folder import | Zero reported leaks |
| Installed OBS 33.0.0, initial capture pass | 6.11.1 | Same checks in Yami and Yami Light | One reported allocation, also present in the foundation's installed-host tests; attribution unresolved |
| Isolated portable OBS 33.0.0 copy | 6.11.1 | Revised layout in both themes, compact window, scale passes, real import and disk counts | Zero reported leaks in each run |

The initial installed-host pass held `installed-obs.lock` and restored the
previous plugin and preferences. W15 later acquired that lock. Subsequent W07
runs left its session and lock untouched. The isolated OBS 33 copy came from
the installed application and lives under `S:/Projects/obs-test/obs-33.0.0-w07`.
Its temporary plugin was removed and its preferences restored after capture.
The portable 30.2 plugin and preferences were also restored. Only processes
started by these test scripts were asked to exit.

Evidence remains in the worktree's ignored `.cache`:

- `obs30-manager.log`: OBS 30.2 host checks and normal unload.
- `yami-obs.log`, `yami-light-obs.log`: final OBS 33 captures and host checks.
- `yami-0.5-obs.log`, `yami-light-0.75-obs.log`: scale passes.
- `visual-review/`: contact sheets used to inspect every theme/state capture.
- `scale100/`, `scale150/`: complete supplemental scale capture sets.

## Screenshots and visual review

[The screenshot index](../images/model-manager/README.md) links all 40 delivered
PNGs. Each theme has all ten model states, first run, the licence acceptance
window, three bundled licence previews, missing-backend results, diagnostics,
a compact window and a real imported-package result. Two additional first-run
images cover 100% and 150% scaling. The main sets use a measured device-pixel
ratio of 2. Metadata records host/Qt versions, logical and pixel dimensions,
device-pixel ratio and cursor exclusion.

The captures show actual dialog client areas rendered by Qt inside OBS. The
capture uses `QWidget::grab`, which excludes the mouse cursor. Model-state
previews inject snapshots and identify themselves in the window. Building and
warming previews exercise the presentation; no GPU build or model timing is
claimed. The separate `import-ready` captures come from an accepted W04 folder
import and a four-byte binder, with measured file counts and cache sizes.

The first visual pass exposed duplicate setup actions and tight table-column
spacing. The revised window keeps one setup action, pads cells consistently
and aligns byte counts to the right. The long TensorRT state label fits without
covering the model name. Error text, licence text and cache paths wrap or scroll.
At 640×460 the content scrolls while the footer remains visible. Native theme
selection and focus colours remain legible in both themes. The 100%, 150% and
200% captures retain crisp text and standard icons. All state captures were
reviewed, with full-size checks of setup, progress, errors, licences, diagnostics,
the real import result and the compact layout.

The licence acceptance checkbox starts unchecked and its action is disabled.
The host test invokes both the filter-properties button and the Tools action,
then verifies one shared manager instance. A real accepted import completes
while that manager is hidden. Core tests separately cover cancelling binding,
retrying from the stored receipt and refusing removal while a reader holds a
lease.

## Remaining integration and test limits

- The production catalogue is empty. Release graph packages, signatures and the
  coordinator's compiled-in public key are required before real downloads can
  be enabled. Cached older revisions are listed for review/removal; they cannot
  authorize new downloads.
- W05 must register its binder before the manager first opens. The default
  adapter reports `CK-E015` and cannot publish incomplete output.
- W06's missing-bundle result was exercised through the dialog. A successful
  native runtime probe, its timings and an actual GPU failure were not exercised
  through this UI. W19 still needs to package the helper, standard-op graphs and
  trusted runtime index described in the integration notes.
- W09 owns live activation, per-filter usage and rollback. W10 must supply real
  TensorRT build events. The manager already presents those states but performs
  no engine build itself.
- W17 must extend the basic allowlisted diagnostics report. No automatic upload
  or issue submission was performed.
- No broadcast, active-recording guard test, full screen-reader session, live
  model inference, GPU benchmark or Linux host test was run. Backend test startup
  checks both public OBS streaming and recording APIs. All manager child
  dialogs and file selectors are modeless.

The user guide, integration notes, screenshot index and this record received a
humanizer pass before commit.
