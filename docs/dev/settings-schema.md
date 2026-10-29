<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Settings schema and integration

`settings::inventory()` describes each persisted processing field, its group, range,
visibility level, disposition and conversion to the W01 `Settings` contract.
`legacy_inventory()` records the remaining prototype fields and their replacements.
`fields.inc` is ordinary checked-in C++; it has no generation step at build time.
The [user reference](../user/settings.md) covers the controls and defaults.

## Control-thread boundary

`set_filter_defaults` fills OBS defaults. `read_settings` reads user values, migrates
and clamps them, writes the normalized record, and returns a new
`shared_ptr<const Settings>`. Call it on the control/update thread. It allocates;
it must not run in `video_render`. W03 should publish that pointer to its scheduler
and attach it to each `InputFrame`. No worker or model preparation is started from
properties callbacks, and no settings code runs in the render callback.

The snapshot keeps requested `target_fps` (zero means follow OBS). W03 must cap its
actual accepted-frame rate by the current OBS rational frame rate. Grid, crop,
source and colour changes require generation handling in W03. Intermediate crop
edits do not request synchronous resets; debounce expensive preparations in the
control path. Reset memory calls `FilterControl::request_reset` once.

UI expansion lives in `advanced`, `expert` and `details_crop/output/sync`, outside
`Settings`. `crop_preview` is cleared when properties open. It is disabled until
W03/W08 supply a temporary preview with a lifetime tied to properties. Saved
`obs_view=crop` remains an explicit output mode. No timer retains a property or
filter pointer.

## Records and migration

The OBS key `settings_version` is 2; the immutable contract's `schema_version` is
also set to 2. `profile` is the named user preset; `grid` stores HxW as a string.
The old W00 `preset`, `model` and `edge_refiner` keys are migration inputs.

Named presets are complete quality/backend/hint/experiment snapshots. Explicit preset
selection resets those fields while retaining crop, sync, output inspection
and a matching CK2 model revision. Editing a quality field
selects Custom. Loading a record does not reapply the preset. An initial property
callback normalizes once, then a private comparison copy distinguishes actual
edits from OBS's initial callback sweep. Numeric drags rebuild the form only for
the first transition to Custom. Crop edits do not rebuild it.

`pipeline_ck1` and `pipeline_ck2` contain independent saved records. `active_model`
identifies the previous pipeline while OBS has already written a new model choice.
A switch saves that previous bank before restoring the other. Only context is shared; stale bank context cannot replace the current crop or
microphone pairing.

For unversioned Lua settings, existing `cut_*` values win independently. Missing
cuts derive from normalized absolute crop edges; right/bottom use `100*(1-bound)`.
An absent edge defaults to the full frame. Migration is idempotent. Lua's named
profiles overrode raw grid, Stage 2, guide mode, tile budget, stagger and previous
hint confidence; migration preserves those effective overrides. It retains other
explicit saved values, including the old 0.035 motion threshold. New instances use
0.05. The runner's scalar JSON arrays are not an import format for OBS `obs_data`;
its engine-side crop/alpha arrays have derived dispositions, while the actual Lua
scalar fields migrate.

`guided_eps` stores exact epsilon. `guided_log_eps` is a UI projection, synchronized
on write; opening properties preserves 0.0005 without rounding it to a slider tick.
Unknown fields stay in the OBS object. A newer schema marker is retained with a
notice while known fields are loaded conservatively.

## Capability input and later packages

`create_filter_properties(control, capabilities)` uses public OBS properties APIs.
An omitted capability map means no verified installed variants. It still exposes
saved choices, but disables unavailable list rows. No capability implies no
working experimental toggle.

Populate `PropertyCapabilities::choices` with string values from the schema:

| Entry | Values |
| --- | --- |
| `ck2.grid`, `ck1.grid` | Installed HxW variants, separately for each family |
| `refiner_model` | Compatible package IDs, such as `v12` |
| `refiner_max_tiles`, `head_max_tiles`, `head_size` | Installed numeric variants represented as decimal strings |
| `backend`, `precision`, `transport` | Successfully probed enum values; `auto` is always offered |
| `device` | Probed adapter IDs; empty selects the OBS adapter |
| `hint_source` | Installed sources; Chroma is always offered. CK2 Stage 1 and MODNet are visible but disabled until available. MediaPipe, external masks and distilled/custom stay absent until declared. |
| `hint_mask` | Validated aligned mask IDs after cycle checks |

The `ck1_refiner`, `head_pass`, `stagger`, `external_mask`, `guided_scale` and
`audio_sync` flags enable their dependent controls. W15 enables `audio_sync` in
the filter caller. Other capabilities must come from verified catalogue and probe
results; callers then request a properties refresh on the control thread.
Unsupported saved values remain in the snapshot; preparation must select
and explain a valid fallback or keep passthrough.

The model manager and diagnostics buttons currently display localized inline
notices. W07 can replace those synchronous callbacks with its modeless dialog.
Reset quality updates the form's referenced OBS data and the owning source using
`obs_source_update`; it does not need a new shared control method. Audio source
lists use `obs_source_get_uuid`, available in the baseline SDK. Legacy source names
are not treated as UUIDs and require explicit reselection. W15 owns sync-offset
execution, ownership and restoration.

Status uses the current `StatusSnapshot`, localized stable error IDs and actual
optional timing values. Information text has an empty label and a separate tooltip
so it cannot widen the form's label column. Missing timings show words instead of
zeroes. W15 refreshes the three status labels on the Qt UI thread at most twice
per second using a weak source reference. It pauses during mouse interaction and popups, and
for 750 ms after a property edit. It updates existing labels without rebuilding
properties. Opening properties or editing a control also refreshes the snapshot.

## Prototype dispositions

| Original fields | Disposition and native behavior |
| --- | --- |
| `profile`; five `preset` grid names; `model`; `edge_refiner` | Active; named profiles, static grid/ROI/stage selection, model family and refinement switch |
| `crop_left/top/right/bottom`, `input_crop`, engine `crop` | Derived absolute bounds; native editing uses percentage `cut_*` |
| `alpha_clip`, `guided_log_eps` | Derived from black/white points and exact epsilon |
| `refiner_path`, `refiner`, `refiner_model` | Active package choice; known version names migrate, arbitrary paths require verified import |
| `external_hint`, `hint_mask`, CK2 hint confidence/mode/blur | Experimental; validated aligned masks and explicit capability checks |
| `auto_audio_source` | Active audio pairing by UUID; legacy names produce a reselection notice |
| `output_width/height`, `engine_resolution` | Derived from the parent source; no fixed bridge size |
| `async_4k_post` | Derived; worker scheduling is always asynchronous |
| `refiner_tile`, halo, band upper cutoff | Derived model constants; v12 center 32, halo 10, upper cutoff .996 |
| `refiner_max_tiles=0` | Retired unbounded live mode; clamp to 64 with a notice |
| `use_cradio` | Retired, fixed off; a legacy true value produces a notice |
| `post_v3`, `compile_post`, `triton_post`, `triton_dw`, `compile_refiner`, `compile_mode` | Retired Python switches; native post path and backend compilation own these choices |
| `engine_auto_start` | Retired; OBS enable/show lifecycle |
| `python_exe`, `project_root`, CLI `package_root` | Retired external-process plumbing |
| Engine start/stop/restart buttons | Retired actions; native enable/bypass/backend recovery |
| Engine log button, `telemetry_status` | Derived status and diagnostics |
| `input_source`, camera/video/dshow/spout-in/synthetic | Retired input selection; OBS parent source or dedicated test tooling |
| `spout`, `spout_name`, `vcam`, `no_preview`, `control_port` | Retired runner transport and control socket |
| CLI `fps`, `max_frames` | Key rate maps to `target_fps`; frame limits stay in test tooling |
| quit/reset commands, reopen flag | Retired remote protocol; teardown and Reset memory |
| `advanced`, `expert`, group expansion, `crop_preview` | Derived UI state outside immutable processing settings |

## Verification

The portable tests cover authoritative defaults, Custom transitions, independent
pipeline banks, Lua migration, clamping and record round trips. The libobs test
covers actual JSON persistence, callback initialization/reopening, group expansion,
Advanced/Expert dependencies, reset behavior, exact epsilon and both locale files.
It runs against either OBS runtime with the baseline-built executable. Set the
runtime's `bin/64bit` first in `PATH` before CTest on Windows.
