<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: implementation work packages

28 October 2026. Inputs: [contribution policy](../../CONTRIBUTING.md), [GPU-runtime linking permission](../../LICENSE-EXCEPTION.md), [third-party notices](../../THIRD_PARTY_NOTICES.md), and [ARCHITECTURE](ARCHITECTURE.md), [MODELS](MODELS.md), [UX_DESIGN](UX_DESIGN.md), [DOCS_PLAN](DOCS_PLAN.md), [ROCM_AMD](ROCM_AMD.md). This is an ordered backlog, not a report of finished implementation. Reconcile the proposed file paths with the existing repository scaffold before changes land.

Sizes are approximate focused engineering effort, excluding waiting for export artifacts, hardware access or maintainer decisions: **S 1-2 days; M 3-5; L 6-10; XL 10+ or split after a spike**. They are planning estimates, not delivery commitments. Hard/risky work is marked **HIGH RISK**. Keep packages reviewable; tests must address behavior or failure modes, not mirror implementation line for line.

## Non-negotiable delivery boundaries

- Public project, English code/UI/docs, GPL-3.0-or-later plus the specified GPU-runtime permission. Free forever, no paid tiers/builds. Use published licence terms for notices.
- No upstream CK or tangentstorm source in GPL files. No CK-derived graphs/weights in Git or installers. No Python on users' machines. Do not port model monkeypatches or `fused_dw.py`.
- Support **ranges and runtime features**, not a single exact OBS/driver/CUDA/TensorRT/ORT/ROCm/distro version. Exact build pins and artifact hashes are reproducibility tools; they must not become arbitrary user-version equality checks.
- Feature-test optional APIs, load unknown/newer versions where safe, and fall back on actual incompatibility. A missing acceleration runtime must not prevent the filter/model manager loading. No implicit live CPU fallback.
- No rendering-thread inference, blocking readback, network, build or worker join. Never recursively render the filter target.
- The export workstream owns CK2 ONNX export and parity. Plugin implementation consumes its contract. Begin with a fake model so this dependency does not block the UI, lifecycle or transfer work.

## Dependency and parallel-work map

```text
W00 scaffold/legal/build
  -> W01 shared contracts + fake pipeline
       -> W02 settings/properties -------> W07 model manager
       -> W03 capture/pacing ------------> W08 portable reconstruction
       -> W04 catalogue/download --------> W05 local binder
       -> W06 runtime/provider probes ---> W09 CK2 integration <--- E01 export
       -> W10 NVIDIA interop (parallel with W11/W12)
       -> W11 DML / D3D12 (parallel with W10/W12)
       -> W12 Linux CUDA/HIP transport
  W08 + W09 + E01 -> W13 native-tile refiner
  W05 + W06 + E02 -> W14 CK1 + hints
  W03 -> W15 delay/audio sync
  W03 + W09 -> W16 multi-instance/lifecycle hardening
  W06 + W07 -> W17 diagnostics/compatibility support
  all required paths -> W18 integration/performance gate
  W00 + W04 -> W19 packaging/CI (develop early; release after W18)
  W02/W07/W15/W17 -> W20 user docs/screenshots
  W18 + W19 + W20 -> W21 release audit
```

After W01, UI/settings, catalogue/binding, rendering, and backend spikes can proceed independently against stable interfaces. W10/W11/W12 own separate platform files; avoid concurrent edits to common frame/session contracts. Change a common contract through W01 review first. W19 builds dummy artifacts early; production packaging waits for the runtime/licence inventory. Optional hint models and experimental head/stagger features cannot delay the first stable CK2 path unless explicitly promoted to release requirements.

## W00: Scaffold, licensing and build floor

Goal: establish a public native plugin that loads without models or GPU runtimes. Inputs: existing scaffold, contribution policy, OBS public API/template and current dependency licences. Files: root CMake/buildspec, `src/plugin.*`, `data/locale/en-US.ini`, `LICENSE`, runtime linking-permission notice, `THIRD_PARTY_NOTICES.md`, CI skeleton. Dependencies: none. Size: M. Risk: medium, especially Qt/runtime packaging.

Acceptance: filter appears under Effect Filters and passes through; no external runtime is a load-time requirement. Build against the proposed OBS 30.2 public API floor; audit symbols before publishing that minimum. Guard optional newer symbols with runtime resolution and feature checks. Test unknown version handling independently of required-symbol failure. Maintain exact dependency build pins while documenting a compatible user range. SPDX/provenance review passes; no CK-derived assets, secrets, large binaries or prohibited source copied. Reconcile file ownership with the existing scaffold instead of rebuilding it in parallel.

## W01: Shared contracts and deterministic fake model

Goal: unblock every plugin workstream before real graphs exist. Inputs: architecture frame/state contract and settings inventory. Files: `src/engine/pipeline.*`, `fake_pipeline.*`, `src/runtime/frame_pool.*`, public settings/status types, synthetic test fixtures. Dependencies: W00. Size: M. Risk: medium.

Acceptance: fake pipeline creates deterministic alpha/foreground from synthetic input, carries sequence/generation IDs, and simulates configurable processing delay, out-of-order completion, OOM, unavailable backend, invalid output and cancellation. It needs no model weights or GPU inference. Define provider/transport capability interfaces, typed errors, lifecycle cancellation and immutable settings snapshots. Test timestamp pairing, generation rejection and bounded queue ownership. Provide a tiny independently authored graph fixture generated in CI for later ORT/binder tests; it contains no CK data. Do not commit a placeholder CK export or let stub quality appear in production presets.

## W02: Settings schema, migration and filter properties

Goal: implement the complete UX inventory with correct defaults and short labels. Inputs: UX_DESIGN, Lua profile/settings inventory. Files: `src/settings/*`, `src/obs/properties.*`, locale files, settings reference metadata. Dependencies: W01. Size: L. Risk: medium. Parallel: W03-W06.

Acceptance: F/B/Q defaults match the specifications, including the 0.05 motion threshold; editing quality produces Custom without resetting crop/audio context. Every original field has an active, derived, experimental or retired disposition. Crop migration distinguishes absolute right/bottom from cut percentages. All ranges/visibility/dependencies/tooltips work; unsupported graphs show a reason. Native group expansion works without private Qt widget access. Advanced/Expert are separate. No automatic stream-visible crop debug on slider movement. A light/dark/high-DPI manual review and serialization/migration checks pass.

## W03: Correct input capture and bounded pacing (**HIGH RISK**)

Goal: acquire the actual filter input and keep the render callback nonblocking. Inputs: W01 fake pipeline, OBS process-filter API, architecture slot lifecycle. Files: `src/obs/filter.*`, `filter_input.*`, `src/runtime/scheduler.*`, staged transport base. Dependencies: W01. Size: L.

Acceptance: camera, image, browser and media source produce correct nonzero input through OBS-supplied filter processing; no recursive `obs_source_video_render(target)`. Multiple scene draws per video tick produce one inference submission. Slow fake inference drops pending work to keep the queue bounded; output pairs with original frame. Next-frame staged mapping additionally checks readiness, never assumes N+1 means complete. Resize/zero-size/filter disable/delete work without use-after-free or waiting in render. Measure CPU callback time and mark a target of p95 <1 ms at 1080p on the reference system, excluding host draw/GPU time; report actual results separately from the target.

## W04: Catalogue, licences and resilient downloads

Goal: install authenticated model artifacts from original publishers. Inputs: MODELS schema, licence records, artifact metadata. Files: `src/models/{manifest,catalog,licenses,download,cache}.*`, schema tests, notice generation. Dependencies: W01. Size: L. Risk: medium. Parallel: W02/W03/W06.

Acceptance: strict schema/signature/hash checks; a manifest cannot execute code or write outside cache. Acceptance precedes payload downloads and records text hashes. HF redirects, Range resume, ignored Range, changed validators, 429, network loss, cancellation, disk full and hash mismatch have deterministic behavior. Use actual destination free space and peak conversion/build budget. Offline import follows the same checks. Downloads/builds deduplicate across instances/processes. Updating preserves the current working revision; removing shared data respects reader leases. A local HTTP test fixture exercises failure cases without repeated large downloads.

## W05: Native weight binder (**HIGH RISK**)

Goal: materialize standard external-data weights without Python or unsafe checkpoint execution. Inputs: MODELS binder spec, synthetic storage fixtures, E01 recipes, exact MoGe/RVM/CK hashes. Files: `src/models/{binder,safetensors,zip_storage,npy,recipe}.*`, independent binder test tool. Dependencies: W04; E01 only for real-artifact validation. Size: XL; split container reader, operations, real validation.

Acceptance: bounded safetensors headers; PyTorch ZIP storage extraction from authenticated metadata without executing `data.pkl`; NPZ numeric arrays with explicit dtype/order and no object deserialization. Checked 64-bit shapes/spans, archive-size/path limits and deterministic cast/transpose/concat/pad operations. Every learned initializer accounted for, including statistics/folded tensors. Hashes and tensor-level parity match exporter outputs; interrupted conversion leaves no ready package. RAM is bounded by chunks and declared transform workspace. Direct safetensors-offset optimization is deferred until materialization works. No arbitrary model-converter scripting language or embedded libtorch.

## W06: ORT loader, capability probes and provider selection (**HIGH RISK**)

Goal: load a compatible runtime bundle and select a backend without risking OBS startup. Inputs: current ORT/EP API docs, architecture fallback chains, fake/tiny graph. Files: `src/engine/ort_session.*`, `src/engine/providers/*`, `tools/backend_probe/*`, runtime build descriptors. Dependencies: W01. Size: L.

Acceptance: optional runtime load; API version negotiation; explicit adapter match; supported precision/operator tests; disposable probe process reports loader crashes/timeouts. Auto considers available TRT-RTX, conventional TRT+CUDA, CUDA, DML or MIGraphX by platform/capability; CPU stays manual diagnostic. Missing APIs disable a provider, not the whole filter. Unknown newer OBS/driver/runtime versions attempt safe probes and produce an untested advisory, not a version-equality error. Matched core/provider binaries and standard-op schemas are validated; prevent runtime-library collisions with another OBS plugin or fail the provider safely with diagnostics. Log provider partitions and reject unacceptable CPU fallback for live profiles.

## W07: Model-manager dialog and first-run flow

Goal: give setup, progress, licence acceptance, disk use and recovery one native home. Inputs: UX wireframe and W04 state machine. Files: `src/ui/model_manager.*`, `license_dialog.*`, `backend_test.*`, frontend menu registration. Dependencies: W02/W04; stub W06 until ready. Size: L. Risk: medium.

Acceptance: accessible modeless dialog from properties and Tools, shared across filters; real state/progress, cancellation semantics, licence text, active users, updates/rollback, import and cache location. Close does not silently cancel an explicit task. Native theme/font, keyboard/high-DPI support, no private OBS Qt access. Test backend starts a bounded explicit test and shows actual provider/transport. Copy diagnostics preview and GitHub issue link exist (W17 fills details). No automatic GPU build while broadcasting; user-initiated setup has a clear progress/recovery path.

## W08: Portable reconstruction and color contract (**HIGH RISK**)

Goal: make the fake/small-grid path produce correct native-resolution transparent output. Inputs: allowed `fused_post.py`/refiner code observations, guided-filter paper, graph color contract when available. Files: `src/gpu/post/*`, `data/effects/{capture,guide,background,finish,debug}.*`, numeric/visual fixtures. Dependencies: W03; E01 only for CK-specific contract validation. Size: L.

Acceptance: implement logical P0-P7/P10-P12 passes independently, FP32 guided solve, pixel-center sampling, delta unmix, push-pull background and post-refinement despill ordering. Premultiplied/straight and sRGB/linear tests catch dark halos/double gamma; source alpha and crop compose correctly. Unsupported HDR passes through visibly. No copied CK color constants or model math in GPL code. Shader outputs match independent small reference fixtures within declared tolerances. Check non-square/odd dimensions, tiny alpha, crop borders and singular covariance; no NaN/Inf output. Record logical-to-fused pass mapping for review.

## E01: External CK2 export contract, owned by export workstream

Goal: deliver model artifacts and reproducible parity evidence; plugin work must not reimplement this exporter. Inputs: upstream models, research engine, published model licences. Files outside plugin repo: `tools/export_onnx.py`, export/parity reports, HF weight-free graphs, bindings and manifests. Dependencies: independent of W00-W08. Size: L/XL, external estimate. Risk: very high numerical/operator compatibility.

Acceptance handoff: static MoGe/RVM/Stage1/Stage2/refiner graphs for required H×W grids and tile batches; exact names/shapes/dtypes/color domains; RVM reset/sequence semantics; MoGe antialiased bicubic convention; stage baton/residual contract; rank-four attention where required; ONNX IR/opset compatible with planned provider ranges. Provide tensor binding maps for safetensors, MoGe ZIP storages, RVM NPZ, including transposes/casts/folded learned data. Pin publisher/graph hashes and licence texts. Supply single-frame and sequence parity tolerances/results, reset/ROI/crop cases, FP16/FP32 variants and failure criteria. Export success alone is not EP execution validation. Learned data stays off the GPL repo.

## W09: CK2 pipeline integration

Goal: replace fake inference with the exported CK2 stage/state machine. Inputs: E01 package, W05 bound artifacts, W06 sessions, research state behavior. Files: `src/engine/ck2_pipeline.*`, `temporal_state.*`, graph contract adapters. Dependencies: W05/W06/E01; W08 for final output. Size: L. Risk: high.

Acceptance: MoGe cache every 3 accepted frames or >.05 motion; RVM each unique accepted frame; per-instance state; Stage 1 auxiliary/default vs Stage 2 Quality; ROI threshold/pad/minimum/EMA/update cadence per spec. Correct reset on source/grid/crop/generation changes, cuts/seeks and large gaps; transforms remain consistent with caches. No C-RADIO. Async slider updates never synchronize the renderer. Compare complete sequences, including first-frame images, against E01; retain parity metrics with exact graph/runtime revisions. Feature flags prevent unvalidated experimental paths from appearing functional.

## W10: Windows D3D11/CUDA and NVIDIA caches (**HIGH RISK**, parallel platform lane)

Goal: fast native-frame access on NVIDIA and persistent backend setup. Inputs: NVIDIA interop docs, permitted bridge lessons, W03 resource lifecycle, W06 sessions. Files: `src/gpu/d3d11_cuda.*`, `src/engine/providers/nvidia.*`, engine-cache service. Dependencies: W03/W06; real graph optional until E01. Size: XL, begin with a 2-3 day spike.

Acceptance: adapter identity, compatible texture registration, private copy fallback, proper synchronization and zero-timeout render-side acquisition. No 32-bit handle assumption, fixed 4K bridge or shared-memory singleton. Native texture → device tensor → result round-trip passes color/alpha tests, resize and two-instance tests. TRT-RTX EP context/runtime cache distinguished from conventional TRT engine/profile/timing cache; stale cache rebuilds once safely. New driver/runtime versions invalidate incompatible cache but do not reject the plugin. Real cold/warm measurements and readback comparison determine auto-enable; no claimed zero-copy/zero-latency shortcut.

## W11: Windows DirectML and D3D12 bridge (**HIGH RISK**, parallel platform lane)

Goal: universal Windows GPU inference with a safe staged baseline and optional sharing. Inputs: DML C API/device tensor docs, static rank-four test graph. Files: `src/engine/providers/directml.*`, `src/gpu/d3d12_dml.*`, D3D11/D3D12 staging wrappers. Dependencies: W03/W06. Size: XL, spike first.

Acceptance: DML sequential/no-memory-pattern settings, no concurrent Run per session, explicit same-adapter queue/device and resource allocation wrapper. Staged input works before sharing. Verify D3D11/D3D12 ownership/fences and buffer packing; a texture is never directly treated as an ORT tensor. Probe static attention and actual CK graphs. Test first on available NVIDIA hardware when scheduling permits, label that correctly, then request community AMD/Intel evidence. DML failure on one graph or driver retains another backend or passthrough. Keep sustained-engineering/WinML decisions documented without adding an unverified deployment dependency.

## W12: Linux GL/CUDA and GL/HIP/MIGraphX (**HIGH RISK**, parallel platform lane)

Goal: support the Linux range and renderer combinations with explicit fallbacks. Inputs: ROCM_AMD, CUDA/HIP GL docs, W03 contracts, matching SDK artifacts. Files: `src/gpu/{gl_cuda,gl_hip,gl_staging}.*`, `src/engine/providers/migraphx.*`, Linux runtime packaging descriptors. Dependencies: W03/W06. Size: XL; separate NVIDIA and AMD subpackages after interface review.

Acceptance: native GL texture/PBO round-trip, correct context/device ownership, no stealing OBS context; EGL and GLX behavior detected at runtime. HIP allocation/stream binding to selected MIGraphX bundle verified independently; no use of obsolete ROCm EP for modern ORT. Failed interop falls to readback. Source size, fence/teardown, adapter mismatch and uninstalled runtime handled. CI compiles/links without AMD hardware; actual AMD behavior stays expected/build-only until a report exists. Runtime checks allow compatible newer distro/driver/ROCm versions. Native library discovery works without a global shell-path modification.

## W13: Native-resolution refiner, selection and tile transport (**HIGH RISK**)

Goal: reproduce v12 detail without full-resolution neural tensors. Inputs: E01 refiner graph/contract, W08 coefficient/background maps, native RGB access. Files: `src/gpu/post/{band,tile_gather,tile_scatter}.*`, `src/engine/refiner_stage.*`, staged atlas transport. Dependencies: W08/W09/E01; W10/W11/W12 for fast native paths. Size: L/XL.

Acceptance: static batches 256/512/768, 32-pixel center/10-pixel halo/52 input for v12; 11 semantic channels and graph-defined padding. Deterministic top-K, valid-slot mask, no duplicate padded writes, correct image-border clamp and feathering toward unselected neighbors. Faint hair and dark/noisy backgrounds match sequence fixtures without seams. Despill/levels applied once after blending. Staged fallback transfers native RGB tiles from the original retained frame, shows added delay and may explicitly disable refinement for performance. Upscaled low-grid pixels cannot satisfy this acceptance. CK1 use remains gated on a separate quality report.

## E02: CK1 and optional hint export packages

Goal: supply CK1 GreenFormer and selected hint graphs without blocking CK2 release work. Inputs: verified CK1 contract/licence evidence, MODNet and optionally MediaPipe originals. Files outside GPL model store: static 256/384/512 CK1 graphs, hint graph, tensor recipes, provenance/parity reports. Dependencies: independent, shares E01 contract conventions. Size: L. Risk: high for low-grid quality and MediaPipe conversion.

Acceptance: code/weights licence scope recorded; CK1 RGB normalization in graph contract, hint linear and same grid, straight sRGB foreground/linear alpha output; green/blue checkpoint distinction. Confirm weight binding coverage and static DirectML attention variant. MODNet first; MediaPipe only if cost benefit and conversion parity justify it. Do not admit PP-Matting or ambiguous checkpoint grants by assuming the code licence covers all weights. No model-specific exporter copied into the plugin repository.

## W14: CK1 pipeline and hint sources

Goal: host CK1 through the same engine abstraction with useful hint controls. Inputs: E02, MODELS hint recommendations, W08 reconstruction. Files: `src/engine/ck1_pipeline.*`, `src/engine/hints/*`, chroma effect, hint preview. Dependencies: W05/W06/W08/E02; W09 only for optional CK2 hint. Size: L. Risk: medium/high.

Acceptance: default chroma hint keeps props; optional MODNet works without requiring CK2; hint/crop transforms align; no invented CK1 confidence tensor. CK2 Stage-1 hint shows dependencies and cost. External mask sources reject dependency cycles and stale/missing frames. Blue selection uses blue checkpoint, correct hint and despill channel. Validate transparency, props and motion alongside the solid person silhouette. CK1-to-refiner validation is separate. Future distilled model slot exists in schema without a fake download row.

## W15: Delay measurement and auto lip-sync

Goal: show useful frame age and optionally align a selected microphone. Inputs: W03 sequence timestamps, OBS sync-offset API, UX. Files: `src/telemetry/frame_timing.*`, `src/obs/audio_sync.*`, sync properties. Dependencies: W03/W02. Size: M. Risk: medium. Parallel: real-model integration.

Acceptance: known fake delays validate displayed age including repeated output; median/p95 and inference/transfer differ clearly. One monotonic timebase and frame-origin propagation. Off by default, explicit source, baseline+delay+trim math, no accumulating offsets, bounded updates/hysteresis. One controller per microphone; manual edits relinquish ownership. Disable/delete/error restores only the last owned value. Rename/delete/shared-mic and source-switch tests pass. A real clap-test guide acknowledges external camera/audio/encoder latency; no claim of complete automatic audiovisual calibration.

## W16: Multi-instance, resolution and failure lifecycle (**HIGH RISK**)

Goal: two cameras and routine OBS lifecycle operations remain stable. Inputs: scheduler, model cache, active providers. Files: runtime scheduler/pool, session registry, source lifecycle tests. Dependencies: W03/W09; run with fake first. Size: L.

Acceptance: two cameras at different resolutions have independent state/crop/settings and fair work admission; shared model downloads/builds do not duplicate or race. Native allocations follow the source, including portrait/odd sizes; never canvas or hard-coded bridge dimensions. Repeated hide/show, resize, scene switch, duplicate source draws, reload, device loss, OOM and delete during download/build/inference recover safely. No unbounded pending queue, stale generation write, lock inversion or render-thread join. 30-minute soak on reference hardware; memory growth and dropped work reported. In-process driver-crash containment limitations are documented honestly.

## W17: Compatibility matrix, diagnostics and GitHub support

Goal: make broad compatibility observable and reportable. Inputs: architecture range policy, DOCS_PLAN issue fields, W06 probes. Files: `src/telemetry/diagnostics.*`, manager report actions, `docs/compatibility.md`, `.github/ISSUE_TEMPLATE/{bug,backend,performance}.yml`. Dependencies: W06/W07; matrix skeleton can start at W00. Size: M.

Acceptance: Copy diagnostics previews redacted plugin/OBS/OS/Qt/GPU/driver/core/EP/runtime/model/graph/transport/settings/timing/error information. No automatic upload, media, token or personal path leak. Report a problem opens issue chooser. Forms include exact environment and reproduction fields, expected/actual, last working version and optional reviewed OBS log. Matrix distinguishes expected/build-only/hardware-tested/community-tested/known-broken with evidence links and exact test anchors inside ranges. Unknown newer versions still load and probe. CI tests missing optional symbols, newer version strings, provider mismatch, safe fallback and real required-ABI refusal with actionable explanation.

## W18: Parity, integration and performance qualification

Goal: establish what can be advertised. Inputs: E01/E02 parity evidence, completed backend/transport paths, test footage with rights. Files: integration tests, `tools/diagnostics` or native benchmark, dated result reports. Dependencies: W09/W13/W15/W16; W14 for CK1 release; backend lanes as claimed. Size: L plus hardware access. Risk: high evidence dependency.

Acceptance: compare model tensors and complete output sequences against declared tolerances. Include edge-band and temporal metrics and transparency/props alongside whole-image MAE. Measure cold setup, warm median/p95 by phase, render callback cost, GPU post time, displayed age, processed FPS, memory and OBS lag under representative streaming/game load. Test refiner off/on and staged/interop separately. Published PyTorch figures are comparison context only. Every claimed platform cell has the right evidence label. No AMD measurement is inferred from compile success or DML-on-NVIDIA. Report failures and narrow feature defaults or choose a fallback accordingly.

## W19: Installers, runtime bundles and public CI

Goal: deliver Windows installer/zip and Linux .deb/tarball with reproducible artifacts. Inputs: scaffold, reviewed runtime licences, model separation and range policy. Files: packaging scripts, CMake install rules, GitHub Actions, release checksums/notices. Dependencies: W00/W04 initially; W06/W10-W12 for final runtimes. Size: L. Risk: high dependency size/ABI distribution.

Acceptance: no models in packages; installed plugin loads without optional GPU runtime. Choose correct x64 host/plugin paths, preserve config/cache on update, separate remove-cache choice. Build against oldest claimed API/OS dependency floor and package separate distro/Qt lanes where necessary. Exact SDK pins remain build metadata; installed users are checked by capabilities and vendor-supported ranges. Versioned runtime bundles cannot silently override other plugins' libraries. Public GitHub-hosted builds only, no unapproved paid/self-hosted runners. Tag builds release candidates and checksum artifacts; maintainers control publication, pushes and repository settings. Clean install/upgrade/rollback/uninstall paths documented and smoke-tested.

W19 must also implement the OBS 33 Windows layout transition described in [ARCHITECTURE](ARCHITECTURE.md): flat module beside `data/` for the new layout, the legacy `bin/64bit/` lane for older hosts, and host-specific portable handling. Verify discovery, upgrades and duplicate suppression across the claimed range. Do not assume the template's current install rules already cover this change. [OBS migration guide](https://obsproject.com/kb/legacy-plugin-developer-info)

## W20: User documentation and real screenshots

Goal: explain every option and common workflow without engineering knowledge. Inputs: DOCS_PLAN, working UI and measured reports. Files: user docs, screenshot asset index, actual PNG captures, optional de-DE locale. Dependencies: W02/W07/W15/W17; final images after W18. Size: M/L.

Acceptance: shot list captured in real OBS with version/settings metadata and permission to use footage; no fabricated UI/performance values. Clean-cache first-run, crop, hair, props/hints, two cameras, sync, offline import and troubleshooting guides work as written. Light/dark/high-DPI examples legible; labels/defaults match schema. Performance guide separated by GPU capability class, AMD status linked, unknown/newer compatibility explained. Do not interrupt active OBS sessions; keep older portable test installations in a separate test directory. Documentation contains no stale Python installation steps.

## W21: Release readiness audit

Goal: release only supported, reviewable behavior. Inputs: W18 evidence, installers, docs, licences and model catalogue. Files: release checklist/changelog, final notices and compatibility table. Dependencies: W18/W19/W20. Size: S/M.

Acceptance: no weights/graphs/secrets in Git or installer; code provenance and GPU permission reviewed; exact model licence acceptance texts included; free-only distribution policy intact. Links, hashes, rollback and first-run tested. Two cameras/source sizing, passthrough/error status, diagnostics redaction, optional runtime absence and audio restoration pass. Every experimental/untested feature labelled or hidden; no false claim of AMD hardware validation or absolute driver-crash immunity. Maintainers receive artifacts, test evidence and unresolved limitations before publication.

## Compatibility verification checklist

Test these version ranges without imposing exact-version support locks:

| Layer | Boundary tests | Compatible-newer behavior |
|---|---|---|
| OBS | Proposed oldest 30.2 API; representative 31/32; stable 33; missing optional symbols | Load, resolve capabilities, advisory only unless required ABI truly incompatible |
| Qt / distro | Host-matched Qt 6; Ubuntu 24.04 dependency floor; newer Ubuntu package | Reuse compatible ABI; separate artifacts for actual dependency breaks |
| NVIDIA | Architecture before/at TRT-RTX floor; driver/runtime minimum supported by selected bundle; recent driver | Probe; CUDA/DML/readback fallback, no version-string equality gate |
| ORT / EP | Minimum C API actually used; current matched bundles; incompatible schema fixture | Negotiate API and graph support; reject provider mismatch safely, not whole plugin |
| AMD Linux | Maintained older ROCm pair and current 10.x pair where artifacts exist; absent runtime | Register/probe MIGraphX; keep expected/build-only label until hardware evidence |
| Transport | Same/cross-adapter, missing sharing/fence API, GL/EGL variants, odd sizes | Staged path or passthrough with exact reason |

## Open decisions and release risks

Implementation can proceed while maintainers resolve these release decisions:

1. **Oldest advertised host:** recommend OBS 30.2+ where public-symbol audit and smoke tests succeed; Ubuntu 24.04 stays supported through compatible host packaging. Confirm whether supporting earlier OBS is worth a separate lane after the audit.
2. **CK1 catalogue wording:** record both HF metadata and GitHub additional terms; confirm the final acceptance wording and attribution against published licence texts.
3. **AMD evidence:** determine when community or borrowed hardware can validate Linux MIGraphX/Windows DML. Until then use expected/build-only labels; paid hardware access or private runners require maintainer approval.
4. **Failure isolation:** recommended in-process live worker plus isolated backend probe meets ordinary exception/fallback handling. If surviving arbitrary native inference crashes is an absolute release requirement, commission a separate runtime-host design, especially Linux cross-process transport, before claiming it.
5. **Release scope:** CK2, CK1/chroma and optional refiner are product requirements. MODNet is the first added semantic hint; MediaPipe is second priority. Head passes, staggering, unlimited tiles, HDR and future distillation remain experimental/backlog unless evaluated and promoted.

Top technical risks are exported CK2 numerical/operator parity; deterministic binding of MoGe ZIP and RVM NPZ; platform synchronization and native-tile transfer; GPU memory contention with two cameras; and runtime/Qt dependency compatibility beside other OBS plugins. Mitigate each with its spike/fixture/probe before optimizing or advertising it.
