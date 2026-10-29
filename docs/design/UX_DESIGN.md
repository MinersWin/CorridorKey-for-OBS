<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: filter and model-manager design

28 October 2026. This is a UI specification, not a screenshot of a finished plugin. Use native OBS properties and Qt widgets. All user-visible text is English by default; keep keys stable for a later German translation. [Architecture](ARCHITECTURE.md) defines timing and fallback behavior; [models](MODELS.md) defines installation and licence rules.

## First-use outcome

A streamer adds **CorridorKey** to their camera, keeps **Balanced**, opens **Models…**, reviews the required licences and download size, and chooses **Accept and download**. OBS continues showing the camera during setup. The plugin verifies, binds, tests and warms the available backend, then begins keying without scripts, a terminal or Python. Crop controls remove the edges of the room; optional lip-sync selects the microphone explicitly.

There are no paid builds, paid tiers, account sign-up or cloud inference steps. Initial setup must not start multi-gigabyte downloads without acceptance. An unavailable GPU produces a useful passthrough status, not an opaque failure or an automatic CPU workload that freezes OBS. On first load, no modal dialog interrupts an active stream: show “Model not installed. Open Models to set up CorridorKey.”

## Properties information architecture

Order in the standard filter pane:

1. **Preset**, then **Model** and **Edge refiner**.
2. A human-readable status line and **Manage models...**.
3. **Crop**, with Show details for its four percentage controls.
4. **Advanced**, which reveals quality/performance, edge/hair, colour/despill, hint,
   output and sync sections. **Expert** adds the experimental and backend controls.

The 29 October visual pass puts the everyday choices ahead of these sections.
A ready status combines model, backend, keyed fps, time per key and delay on one
line. Error codes and raw backend messages belong in its tooltip and diagnostics.

Use the native `obs_properties_*` API, long descriptions for tooltips, modified callbacks and `obs_property_set_visible` for conditional fields. Ordinary groups are **not guaranteed collapsible** by `OBS_GROUP_NORMAL`. Implement “Show details” buttons/booleans that toggle child visibility inside each group; keep expansion state in UI preferences, separate from processing settings. Do not inject widgets into OBS's private QFormLayout. [Properties API](https://docs.obsproject.com/reference-properties)

Labels should normally fit within 8-16 English characters; 20 is an upper guide, not a reason to use cryptic abbreviations. Units go in the control suffix or tooltip. Keep the longest label short because it sets OBS's form label-column width. No emoji prefixes. A disabled control explains its dependency; an unsupported graph is not a working toggle.

**Advanced** reveals A rows below. **Expert** is an additional checkbox inside Advanced and reveals E rows. D rows are ordinary controls, possibly inside a collapsed group. R rows are read-only or automatic. X rows are retired prototype plumbing; they are mapped explicitly and not exposed as inert controls. Conditional availability is in addition to D/A/E visibility.

Presets store a versioned complete snapshot. Editing a preset-owned quality control changes Preset to Custom while preserving the previous snapshot; an explicit preset selection reapplies that preset. Crop, audio-source selection, delay trim and output inspection are user context and survive preset changes. Save/load retains effective settings and model revision. If an updated model changes defaults, existing instances keep their saved values.

## Preset defaults

All dimensions below are **H×W**. F/B/Q means Facecam 60 / Balanced / Quality. A single default in the control tables means the same value in all three; `F/B/Q` values override it. Custom initially clones Balanced, or the profile being edited. The measured prototype profiles apply to **CK2**; do not transfer their performance claims to CK1.

| Setting | F | B (default) | Q |
|---|---|---|---|
| Pipeline | CK2 | CK2 | CK2 |
| Grid | 256×512 | 512×512 | 512×512 |
| Subject ROI | Off; full user crop | On | On |
| Stage 2 | Off | Off | Every frame |
| Refiner | v12, on | v12, on | v12, on |
| Tile budget | 384 | 512 | 768 |
| Target key rate | Up to 60 FPS, capped by OBS | Follow OBS | Follow OBS |
| MoGe interval / motion | 3 / 0.05 | 3 / 0.05 | 3 / 0.05 |
| RVM memory | On | On | On |
| CK2 alpha hints | Off (quality 0) | Off | Off |
| Guided radius / epsilon | 2 / 0.0005 | 2 / 0.0005 | 2 / 0.0005 |
| Despill / contrast / choke | 0.65 / 1.05 / 0 | Same | Same |
| ROI pad / smoothing | 0.20 / 0.80, dormant | Same | Same |
| Band threshold / gain | 0.004 / 10 | Same | Same |
| Unmix / interior amount | Delta / 1.0 | Same | Same |
| Crop / black / white | All 0% / 0 / 1 | Same | Same |
| Aspect preservation | Off, reference-compatible | Off | Off |
| Stagger / head pass / C-RADIO | Off / Off / unavailable | Same | Same |
| Output / auto sync | Key / Off | Same | Same |

For CK1, choose **Custom** automatically on first selection, starting at 384×384 with MODNet, RGB guided reconstruction and refiner off. The revision-4 owner decision keeps CK1 Advanced-only and labels it Experimental, with an honest quality note and the CK2 Facecam/refiner-off fallback recommendation. Offer the released 256/384/512 grids. CK2 Stage-1 alpha shows its dependencies and cost; chroma requires Expert and warns about teal or uneven screens. Per-pipeline saved settings preserve the user's CK2 setup. The learned refiner stays off until an evaluated CK1 package exists.

The specified 0.05 motion threshold supersedes the Lua/CLI 0.035 default. Radius 2 and epsilon 5e-4 are authoritative. The shipped Facecam tile budget is 384: against 256 tiles it measured 5–9 % lower edge error for about 0.5 ms more on the reference GPU.

## Controls: model and performance

| Key / prototype mapping | Label | Visibility; range / choices | F/B/Q default | Tooltip |
|---|---|---|---|---|
| `profile` | Preset | D; F/B/Q/Custom | B selected | Start with Balanced. Facecam 60 prioritizes speed; Quality runs an extra detail stage. |
| `advanced` | Advanced | D; boolean | Off | Show detailed controls. Changing them may increase delay. |
| `expert` | Expert | A; boolean | Off | Show experiments and backend settings. Availability depends on the installed model. |
| `model_id` | Model | D; CK2/CK1 | CK2 | Choose a keying pipeline. Models have separate licences and downloads. |
| model-manager action | Models… | D; button | N/A | Install, update, import or remove models and test your backend. |
| `backend` | Backend | A; Auto / available TRT-RTX / TensorRT / CUDA / DirectML / MIGraphX / CPU diagnostic | Auto | Auto chooses a backend that passes compatibility checks. The active backend is shown in Status. |
| `device` | GPU | E; Auto or enumerated adapter | Auto: OBS adapter | Prefer the GPU OBS uses. Another GPU may require slower transfer. |
| `target_fps` | Key rate | A; Follow OBS, 15, 24, 25, 30, 50, 60; never exceed OBS rate | 60 / Follow / Follow | Limit how often new frames are keyed. OBS still renders at its normal rate. |
| `preset` → grid + ROI | Grid | A; installed static variants; CK2 256×512, 512×512, 512×768, 512×1024 | 256×512 / 512² / 512² | The model's working size, not the camera size. Larger grids use more time and memory. |
| `roi_enabled` | Subject crop | A; boolean, CK2 | Off / On / On | Focus the model on the foreground inside your manual crop. |
| `stage2_interval` | Detail interval | A for Off/Every; E for 2-4 | 0 / 0 / 1 | Off uses Stage 1. Values above 1 reuse detail and may leave trails. |
| `refiner_max_tiles` | Tile budget | A; manifest batches, originally 64-1536 in steps of 64 | 384 / 512 / 768 | More native-resolution edge tiles can improve hair but take longer. Only installed graph batches are available. |
| `guided_scale` | Guide scale | E; 1.0 / 0.5, only for paths that implement it | 1.0 | Reduce the legacy guided pass resolution. It does not reduce the refiner's native tile sampling. |
| `aspect_fix` | Keep aspect | E; boolean | Off | Preserve source proportions in the model grid. May change results from measured presets. |
| `moge_interval` | Geometry rate | E; 1-8 accepted frames, step 1; CK2 | 3 | Refresh geometry at this interval, or earlier when motion exceeds the threshold. |
| `moge_motion_threshold` | Motion limit | E; 0.005-0.15, step .005; CK2 | .05 | Mean RGB change in a 32×32 thumbnail that triggers a geometry refresh. Lower refreshes more often. |
| `roi_padding` | ROI margin | A; .05-.50, step .05; CK2 ROI | .20 | Add margin around the detected foreground so moving hands and hair remain in view. |
| `roi_smoothing` | ROI smoothing | E; 0-.95, step .05; CK2 ROI | .80 | Higher values move the automatic crop more slowly. |
| `use_rvm_temporal` | Video memory | A; boolean; CK2 | On | Use previous frames for temporal features. Reset after a cut or source change. |
| reset-state action | Reset memory | A; button | N/A | Clear recurrent state, geometry cache, crop tracking and previous hints. |
| `stagger_s1_s2` | Alternate stages | E; boolean, capability-gated | Off | Experimental alternating Stage 1/2. The prototype flickered; not recommended for streaming. |
| `precision` (new) | Precision | E; Auto / tested FP16 / FP32, BF16 only if declared | Auto | Use only precisions validated for this graph and backend. Lower precision is not always faster. |
| `transport` (new) | Transfer | E; Auto / Interop / Staged; availability-tested | Auto | Interop keeps pixels on the GPU. Staged transfer can add delay; see Diagnostics. |

The old five `QualityPreset` values map as follows: `fast_4k_512x256_s1s2` → H256/W512 full frame; `roi_ultra_4k_512x512_s1s2` → 512² ROI; `high_aux_4k_1024x512_s1` → H512/W1024 auxiliary; `quality_4k_768x512_s1s2` → H512/W768 with Stage 2; `extreme_async_4k_1024x512` → H512/W1024 experimental Stage-1 reuse. Old names are migration inputs, not promises that every profile always uses Stage 2 or outputs 4K. Only manifest-supported variants can activate; unavailable legacy variants retain settings but use a stated fallback.

## Controls: edge, colour and crop

| Key / mapping | Label | Visibility; range / choices | Default all, unless noted | Tooltip |
|---|---|---|---|---|
| `refiner_enabled`, derived from `guide_mode` | Refine edges | D; boolean | On for CK2 | Recover fine edges from native camera pixels. Needs the edge-refiner model and a supported transfer path. |
| `guide_mode` | Edge method | A; Refiner / RGB guided / Bilinear; E adds Chroma legacy | Refiner | RGB guided is the fallback when refinement is unavailable. Bilinear shows the raw model detail limit. |
| `refiner_model` / `refiner_path` | Refiner | A; installed v12, v10w, v9b; legacy v4 only if a compatible graph exists | v12 | v12 is the current reference. Older versions are for comparison and compatibility. |
| `guided_radius` | Hair radius | A; 1-12 low-grid pixels, step 1 | 2 | Neighborhood used for the guided filter. Larger values can smooth detail. The effect scales with the grid. |
| `guided_log_eps` / `guided_eps` | Edge smoothness | E; log10 epsilon −5 to −2, step .1; exact epsilon stored | log10(.0005) ≈ −3.30103 | Regularize the guided filter. Larger epsilon smooths more. Default is 0.0005. |
| `alpha_contrast` | Matte contrast | D; .70-1.50, step .01 | 1.05 | Increase separation around mid-opacity. Too much removes translucent detail. |
| `alpha_offset` | Matte choke | D; −.20-.20, step .005 | 0 | Negative values shrink opacity; positive values expand it. Check hair and motion blur. |
| `clip_black` / `alpha_clip[0]` | Black point | A; 0-.50, step .005 | 0 | Opacity at or below this value becomes transparent. |
| `clip_white` / `alpha_clip[1]` | White point | A; .50-1, step .005 | 1 | Opacity at or above this value becomes solid. Must exceed Black point by at least .01. |
| `refiner_band_lo` | Band minimum | E; 0-.10, step .001; exact .004 allowed | .004 | Lowest network alpha considered for edge refinement. Raising it can lose faint hair. |
| `refiner_band_gain` | Band gain | E; 0-20, step .5 | 10 | Weight refinement by nearby foreground. Larger positive values allow more weight at low alpha. Zero disables this weighting. |
| `refiner_tile` | Tile size | R, E; manifest-defined, 32 for v12 | 32 | Native center size; halo and batch shape are fixed by the installed refiner graph. |
| `unmix_mode` | Colour method | A; Delta / Compositing equation | Delta | Delta transfers model color correction onto native detail. The alternative is experimental. |
| `unmix_inner` | Interior colour | A; 0-1, step .05 | 1 | Amount of model color correction in solid areas. Edges retain full correction. |
| `despill_strength` | Despill | D; 0-1, step .02 | .65 | Remove green spill after refinement. Reduce if neutral or colored details shift. |
| `screen_color` (new) | Screen | D for CK1; Green/Blue | Green | CK1 uses a separate checkpoint for blue screens. CK2 v12's current presets are validated for green. |
| `cut_left` | Left | D; 0-45%, step .5 | 0% | Cut this edge before model inference. Pixels outside the crop become transparent. |
| `cut_right` | Right | D; 0-45%, step .5 | 0% | Remove room edges outside the screen. The camera output size stays unchanged. |
| `cut_top` | Top | D; 0-45%, step .5 | 0% | Cut from the top. Keep space for hair and movement. |
| `cut_bottom` | Bottom | D; 0-45%, step .5 | 0% | Cut from the bottom. Anything inside the screen foreground remains eligible. |
| `input_crop`, old `crop_left`, `crop_top`, `crop_right`, `crop_bottom` | Crop bounds | R; normalized left/top/right/bottom, derived | (0,0,1,1) | Internal crop representation; edit the percentage controls. |
| `crop_preview` | Preview crop | D; boolean, temporary | Off | Show the removed area in the output for inspection. This view can appear on stream. |

Enforce valid crop extents after migration. Legacy right/bottom are absolute boundaries, not cut percentages: convert `cut_right = 100*(1-right)`, likewise bottom. Do not multiply normalized crop fields twice. Avoid resetting temporal state on every intermediate mouse event: debounce expensive changes and carry a generation ID.

Two deliberate prototype corrections: the original epsilon slider extended below the engine's effective 1e-5 floor, so the new UI does not offer ineffective values; the old band-gain tooltip said larger is stricter, whereas the actual clamped multiplication allows more weight as gain increases. The band upper cutoff .996 and v12 halo 10 are model contract constants, not additional ordinary sliders.

## Controls: hints and output

| Key / mapping | Label | Visibility; range / choices | Default | Tooltip |
|---|---|---|---|---|
| `hint_source` (CK1) | Hint source | D; Chroma / MODNet / MediaPipe if installed / CK2 Stage 1 / imported mask | Chroma | A rough foreground guide. Person models can exclude props, chairs or a second person. |
| `hint_similarity` (new) | Screen match | A, chroma; 0-1, step .01 | .40 provisional | How closely normalized chroma must match the screen to seed background. Calibration pending; preview Hint. |
| `hint_smoothness` (new) | Hint softness | A, chroma; .01-.50, step .01 | .10 provisional | Width of the chroma transition. This changes the guide, not final matte sharpness. |
| `hint_refresh` (new) | Hint rate | A; 1-8 accepted frames, step 1 | 1 | Reuse semantic hints between updates. Motion may expose a delayed guide. |
| `hint_mask` / engine `external_hint` | Mask source | E; imported validated mask or selected OBS source with cycle checks | None | Optional full-frame linear mask, aligned to this frame and crop. A missing mask uses the selected fallback. |
| `hint_mode` (CK2 experiment) | Hint mode | E; Off / Previous / Chroma / Blend / External | Off; stored legacy mode carry with quality 0 | CK2 works best without alpha hints in the measured profiles. These are experiments. |
| `temporal_hint_quality` | Previous hint | E; 0-.95, step .05; CK2 only | 0 | Trust in the previous alpha hint. Zero disables it; high values can cause trails. |
| `chroma_hint_quality` | Chroma trust | E; 0-1, step .05; CK2 only | .75, dormant | Trust passed to CK2 for a chroma hint. This is not a native CK1 model input. |
| `external_hint_quality` | Mask trust | E; 0-1, step .05; CK2 only | .50, dormant | High confidence can make CK2 copy the rough mask. Prototype raw default was .95; the safer experiment starts at .50. |
| `external_hint_soft` | Mask blur | E; 0-64 inference pixels, step 1 | 0, dormant | Box-blur the external hint twice. This prepares a coarse guide; it is not final-alpha blur. |
| `obs_view` | View | D; Key / Alpha / Hint / Foreground / Crop; A adds Input / Tile coverage / ROI | Key | Debug views replace the filter's output and may appear on stream or recording. |
| `view_mode` (runner preview) | Inspect | A; Transparent / Checker / Black / Alpha / Side by side | Transparent | Inspect the keyed image; Side by side shows keyed-on-black and alpha. This can change output when explicitly enabled. |
| `bg_color_rgb` | Preview colour | A; RGB picker, each 0-1 | Black (0,0,0) | Inspection background only. Normal Key output remains transparent for the OBS scene. |
| `bypass` (new) | Bypass | D; boolean | Off | Show the original source and stop inference submissions. |

The runner default was Checker; the native filter deliberately defaults to transparent Key output. Its `checker`, `black`, `alpha`, and `side_by_side` modes all retain explicit inspection equivalents. Imported preview settings never silently replace normal stream output.

The crop slider must **not silently replace program output for four seconds**, as the Lua prototype did. If OBS exposes no separate safe property preview, require the explicit Preview crop control, clearly state that it changes output, and turn it off on properties close. A four-second timeout after the last edit is permitted only after the user enabled that view. Reset to Key on reopening unless the user intentionally saved an output debug mode.

For CK1, no learned hint is downloaded automatically by choosing its row. A missing hint opens the relevant model entry and keeps chroma or passthrough, according to the explicit setting. CK2 Stage-1 hint shows its dependencies and extra work. Future distilled hints are absent from the installable list until a real manifest exists.

## Controls: sync and expert experiments

| Key / mapping | Label | Visibility; range / choices | Default all | Tooltip |
|---|---|---|---|---|
| measured delay | Filter delay | R; median ms, p95 in tooltip | Not measured | Age added from this filter's captured input to its displayed result. Does not include camera or encoder delay. |
| `auto_sync` / `auto_audio_source` | Auto lip-sync | D; Off / On, source picker required | Off / None | Add this filter's measured delay to one selected audio source. Other camera and audio latency may still need adjustment. |
| selected audio UUID | Audio source | D when sync enabled; actual audio sources | None | Choose the microphone paired with this camera. One filter can control its offset at a time. |
| `sync_trim_ms` | Sync trim | A; −250-250 ms, step 1 | 0 | Extra correction after the measured delay. Verify with a clap recording. |
| `sync_limit_ms` | Delay limit | E; 50-1000 ms, step 10 | 250 | Maximum extra audio delay this filter can apply. Large delays do not fix a backend that cannot keep up. |
| `stale_timeout_ms` | Stale limit | E; 100-1000 ms, step 10 | 250 | Return to live passthrough if no fresh keyed result arrives within this time. |
| applied offset | Applied delay | R; ms and owning filter | Not measured | Actual added offset and whether it is stable, paused, or controlled elsewhere. |
| `head_pass` | Head detail | E; boolean, graph capability required | Off | Experimental second Stage-1 pass. Measured prototype cost was roughly 20-30 ms extra. |
| `head_size` | Head grid | E, head pass; installed 256-multiple grids, initially 512/768 | 512 | Working size of the separate head pass; loads another graph/state. |
| `head_max_tiles` | Head tiles | E, head pass; installed refiner batches | 256 | Native refinement budget for the head pass. |
| `head_min_gain` | Min. head gain | E, head pass; 1-4, step .1 | 1.3 | Run the head pass only if it samples the head at least this much more finely. |
| reset defaults | Reset quality | A; button | N/A | Restore the selected quality profile, keeping crop, audio pairing and installed models. |
| diagnostics action | Diagnostics… | A; button | N/A | Open backend results, timing details and Copy diagnostics. |

Negative sync trim does not imply the plugin can make already-delayed audio arrive earlier. Show the computed total, retain the baseline and respect OBS's actual API limits. Audio source names are display-only; use a persistent ID where the host supports one, with safe weak-reference fallback. Manual offset edits disable automatic ownership instead of fighting the user.

## Complete disposition of remaining prototype settings

Implementation and migration must account for every `EngineConfig` field, Lua control and runner transport setting:

| Prototype key(s) | Native label / range / default | Visibility / disposition |
|---|---|---|
| `output_width`, `output_height`, Lua `engine_resolution` | Source size; actual current W×H; default Follow source | R. Remove fixed 720p/1080p/4K bridge sizing. Native output tracks source; graph grid remains separate. |
| `use_cradio` | C-RADIO; fixed Off | X. Outside the release scope; discard legacy true with migration notice. |
| `async_4k_post` | Async processing; automatic On | R. Architecture schedules off render thread; no user CUDA-stream checkbox. |
| `post_v3` | Post path; automatic verified fused path | X. One native parity-tested path plus explicitly supported edge/unmix alternatives. |
| `compile_post`, `triton_post` | Shader optimization; automatic | X. PyTorch/Triton switches do not apply to C++ effects/kernels. |
| `triton_dw` | Network optimization; graph-owned | X. No port of `fused_dw.py`; exporter/backend responsibility. |
| `compile_refiner` | Refiner optimization; automatic | X. ORT backend compiles supported static tile batches. |
| `compile_mode` | Backend build mode; Auto | X. No Python compile-mode string. Test/Optimize backend is the native action. |
| `refiner_path` filesystem path | Refiner; installed model ID | A. Covered above; no arbitrary `.pt` path. Offline import verifies known formats/hashes. |
| `refiner_max_tiles=0` | All tiles; expert offline diagnostic only | E. Not a live default or unbounded dynamic graph. Repeated fixed batches may implement it only with an explicit diagnostic cap. |
| `engine_auto_start` | Enabled; follows OBS filter enable/show state | X. No external engine process to auto-launch. Model setup remains user-initiated. |
| `python_exe`, `project_root`, CLI `package_root` | N/A | X. Removed; no Python or research checkout required. |
| `btn_engine_start`, `btn_engine_stop`, `btn_engine_restart` | Enable / Bypass / Retry backend | X→native lifecycle actions. Retry after an error does not restart OBS. |
| `btn_engine_log`, `telemetry_status` | Open log / Status | D/R. Native diagnostics and status described below. |
| `input_source`, CLI camera/video/dshow/spout-in/synthetic input | Filter input; parent source | X. Camera choice belongs to OBS. Synthetic input belongs to test tooling. |
| `spout`, `spout_name`, `vcam`, `no_preview`, `control_port` | N/A | X. No Spout, virtual-camera or UDP transport in native filter. |
| CLI `fps`, `max_frames` | Key rate / diagnostic frame count | A for key rate; test-tool-only frame limit. OBS controls output FPS. |
| `command=quit`, `command=reset_state`, runner reopen flag | Filter teardown / Reset memory | X→native actions; no public remote control socket. |

## Status line

Place two read-only lines directly below Model, with no long label column:

`Ready · CK2 Balanced · TensorRT-RTX`

`14.8 ms/key · 34 ms delay · 58 keyed FPS`

Those numbers are **illustrative**, not measured C++ performance. Display a stable model/profile/backend on line one. On line two, show rolling median processing time, actual displayed-frame age and processed FPS. Tooltip/details show p95, transfer mode, source dimensions, dropped submissions, repeated output, cache and active instances. Do not label inference time “latency”. Use “Warming up” instead of zero milliseconds.

Actionable variants:

- `Not ready · Model not installed` / `Open Models to set up CorridorKey.`
- `Downloading · CorridorKey 2.0` / `1.2 of 3.1 GiB · 18 MiB/s` (only if actual totals match).
- `Preparing · Building TensorRT engine` / `Stage 1 · first setup may take several minutes`.
- `Ready · DirectML · Guided edges` / `Refiner unavailable on this transfer path. Open Models for details.`
- `Untested combination · Using staged transfer` / `Keying is active. Copy diagnostics if you notice a problem.`
- `Passthrough · GPU memory exhausted` / `Try Facecam 60 or close another model instance.`
- `Passthrough · Backend stopped responding` / `Retry backend · code CK-E104` (error code illustrative until defined).

Numeric updates at most twice per second; state changes immediately. `obs_source_update_properties` can rebuild the form: do not repeatedly call it during a slider drag or active text edit. Refresh through supported APIs on the control thread, coalesce updates, and defer numeric refresh while interaction is active. No timer retains a raw property pointer past destruction. Full-rate telemetry remains in the model-manager details view.

## Qt model manager

Open one modeless dialog from the filter's **Models…** button or **Tools → CorridorKey Models…** using public `obs_frontend_add_tools_menu_item` and the OBS main window as parent. Selecting a filter only changes the “Used by” context; downloads are shared globally. The frontend API provides the entry point, not a custom model-manager widget. [Frontend API](https://docs.obsproject.com/reference-frontend-api)

ASCII layout, target logical size approximately 760×560, resizable, minimum 640×460:

```text
CorridorKey Models                                         [close]
Models run on this computer. Camera video is not uploaded.

Model                         State          Download   Installed
> CorridorKey 2.0              Ready             --       5.8 GiB*
  Edge Refiner v12             Ready             --        92 KiB*
  CorridorKey 1.0              Not installed   380 MiB*       --
  MODNet hint                 Not installed      ...         --
----------------------------------------------------------------
CorridorKey 2.0
By Corridor Digital / Niko Pueringer
Used by: Camera A, Camera B
Licence: Corridor Key Licence 2.0                 [View licence]
Backend: Auto -> TensorRT-RTX     Compatibility: Untested
Source: Follow source            Profile: Balanced

[Install / Update] [Remove...] [Import...]        [Test backend]
----------------------------------------------------------------
Preparing weights         [======================       ]
Step 3 of 5 · 2.1 GiB written                [Cancel this task]

Cache: <selected model-cache folder>               [Change...] [Open]
Models 3.1 GiB* | Bound data 1.6 GiB* | Builds 1.1 GiB*
Available: 420 GiB*                              [Clear builds...]
[Copy diagnostics] [Report a problem]                    [Close]
```

`*` values in this wireframe are placeholders for layout only. The product displays actual bytes and separates shared/source data, locally bound data and backend caches. No claim of fixed installed size is made.

Use a simple table/list and one details panel. Model families own their dependencies; the detailed dependency list expands on demand. Avoid a gallery of marketing cards, hero images, badges or “recommended” stickers on every row. Model display names do not expose internal file formats unless the user opens Details.

| State | Main action | Secondary behavior |
|---|---|---|
| Not installed | Review and install | Show total missing dependencies and peak disk requirement |
| Awaiting acceptance | Accept and download | Decline leaves passthrough; acceptance is never prechecked |
| Downloading | Pause / Resume or Cancel | Real bytes and rate; cancelling preserves resumable partial data |
| Verifying | Cancel | Identify current file; mismatch offers Retry, never Use anyway |
| Preparing weights | Cancel at safe boundary | Explain local one-time conversion; no Python details in main flow |
| Building TensorRT engine | Stop after this step | Indeterminate progress unless real progress is available; old model stays active |
| Warming up / Testing | Cancel | Fixed test scope; no camera upload |
| Ready | Test backend | Use in selected filter if appropriate; Used by list |
| Update available | Review update | Current revision stays active; show new licence/dependency changes |
| Error | Retry | Human-readable cause, code, details, Copy diagnostics; preserve old ready revision |
| Unsupported backend | Choose backend | Explain the missing feature and available fallback alongside any version mismatch |

**Licence flow:** show the model family and each distinct dependency licence with publisher, version, scope and full text/link. CK-derived packages use the exact Corridor Key version and required attribution; list other foundations under their own terms. The summary must not conceal the large-organisation rule in v2 or imply that the GPL plugin relicenses weights. One explicit “I accept these model licences” checkbox and **Accept and download** button record all visible licence hashes. No legal quiz or paid-tier upsell. Licence changes require a new acceptance; unchanged re-downloads do not.

**Test backend:** open a small native probe process, use a generated synthetic frame, verify graph load and output sanity, and report backend/transport, supported features, cold build time, warm median/p95, memory estimate and tested profile. Show an explicit Start test action if OBS is streaming/recording and the test would compete for GPU resources. Probe uses no camera frame unless the user explicitly chooses a later live test. Failures return a result even if the probe crashes. Close the process and do not leave a hidden benchmark running.

**Copy diagnostics:** copy a previewable, redacted text report: plugin/build; OBS/Qt/OS/distro/kernel; GPU/VRAM/adapter match/driver; ORT, EP, CUDA/TensorRT or ROCm versions; feature-probe results; selected and rejected backends with reasons; transport; model/graph hashes; source dimensions/color/FPS; settings differences; timings and error codes. No frames/audio, tokens, usernames, full paths or scene/source names by default. **Report a problem** opens [the GitHub issue chooser](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose), never auto-submits a report. Diagnostics must say “untested” for an unknown newer version that passes capability checks; “unsupported” would be inaccurate.

## Visual language and accessibility

Inherit OBS's Qt font and palette. Use the host body size (typically about 10-11 pt on Windows), 600 weight for section titles only, 8 px related-control spacing, 16 px group gaps and 16 px dialog margins, adjusted by Qt style metrics and DPI. Use the authored GPL monochrome SVGs in `data/icons`, rendered at 16 logical pixels in the host palette. The icons share a 1.5 px stroke and round line joins. Keep verbs consistent: Install, Pause, Resume, Retry, Remove, Test. Align numerical units and use plain text for status as well as icons.

Use palette-derived text/base/button/highlight colors so Yami Dark, Yami Light and system high-contrast modes remain readable. Green means ready, amber means degraded or waiting, red means failure; each always has a word/icon. Custom semantic tints, if necessary, must be checked in both light and dark themes at accessible contrast. Use blue/host highlight for selection, not decorative glows. Keyboard focus, tab order, screen-reader names, accessible progress text and 100/150/200% scaling are release checks.

Do not ship a custom font, purple gradients, glass panels, huge rounded cards, animated sparkles, emoji section headings, all-caps labels, unexplained sliders, permanent skeleton loaders or tiny grey legal text. Do not hard-code dark backgrounds or restyle the whole OBS application. Do not turn every slider adjustment into a toast. Reserve inline status for state; use a modal confirmation only for destructive removal or cache relocation with active models.

## First-run and recovery sequence

1. Add filter; show live camera and Balanced selected. Detect GPU features asynchronously; “Detecting backend…” is a state, not a stalled property window.
2. Open Models; recommend CK2 plus v12, explain the one-time download and actual available space. Offer change location before download.
3. Accept exact model licences. Download/resume, verify and bind with persistent progress; closing the dialog keeps the explicit task running.
4. Test a compatible backend and build the selected profile only. If a preferred EP fails, try a supported fallback and state it. Unknown versions are allowed through capability tests.
5. Activate the result atomically. A concise message identifies Key, Backend and Delay. No performance promises from the PyTorch prototype appear in the setup flow.
6. Guide the user to Crop if room edges remain. Explain that foreground includes objects in front of the screen. Do not silently choose a person-only hint.
7. Offer optional Auto lip-sync with microphone selection and a clap-test link. Keep it off until selected.
8. If frame budget is missed, suggest Facecam 60, fewer tiles or guided edges; do not change image quality repeatedly in the background. Save effective choices for the next launch.

Design precedents: LocalVocal 0.6.2 uses a model download dialog and separate worker, and supports external models. Its task/progress separation is a design reference; do not copy its code. BackgroundRemoval's inspected tree carries per-model licence sidecars, a useful provenance practice; current latest release observed was 1.4.1. OBS's NVIDIA video filters gate controls/status on SDK availability/version, a useful pattern for explaining backend readiness. [LocalVocal](https://github.com/royshil/obs-localvocal/releases/tag/0.6.2), [download UI source](https://github.com/royshil/obs-localvocal/blob/0.6.2/src/model-utils/model-downloader-ui.cpp), [BackgroundRemoval](https://github.com/royshil/obs-backgroundremoval/releases/tag/1.4.1), [OBS NVIDIA filter](https://github.com/obsproject/obs-studio/blob/33.0.0/plugins/nv-filters/nvidia-videofx-filter.c). These are research observations, not claims that their model-management behavior matches this specification.
