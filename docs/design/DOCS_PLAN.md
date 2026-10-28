<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: documentation plan

28 October 2026. Ship documentation with the plugin. The reference must explain every available setting and accurately distinguish the native plugin, the research prototype, and proposed features. No real OBS screenshots were captured during this research task; the shot list below specifies the required production captures.

## Information structure

Use repository Markdown as the canonical source. A small static documentation site can render the same files later; no second independent manual. Begin each guide with its outcome, prerequisites and supported platform, then numbered actions with real screenshots. English is canonical; German may follow using the same option keys and screenshot IDs.

| Page | Reader outcome / required content |
|---|---|
| `README.md` | What CorridorKey does, free-project statement, supported/expected platforms, install links, model licence distinction, quick start, support links. No blanket “60 FPS on every GPU”. |
| `docs/start.md` | Install plugin, add filter, install CK2/refiner, choose preset, crop, test output and save scene. Short enough for a streamer to finish without reading architecture. |
| `docs/install/windows.md` | Installer vs zip, correct system plugin location, standard host versions, graphics-runtime discovery, upgrade/uninstall and model-cache retention. No Python instructions. |
| `docs/install/linux.md` | Ubuntu 24.04 first, separate newer distro/OBS packaging where needed; .deb/tarball; host Qt/libobs compatibility; native vs Flatpak distinction. Never suggest installing a native .deb into a Flatpak sandbox. |
| `docs/models.md` | Pipelines and dependencies, original publishers, exact licence versions/attributions, download progress, resume, local binding, first build, updates/rollback, offline import, moving cache and disk accounting. |
| `docs/options/index.md` | A screenshot of the default pane and short map of groups. Explain presets vs Custom, Advanced vs Expert, model-dependent controls and disabled controls. |
| `docs/options/{model,performance,edges,colour,crop,hints,output,sync,expert}.md` | Every setting in UX_DESIGN, including unit, default per preset, effect, when to change it, cost, dependency and failure/fallback. Cover preset changes as well as individual sliders. |
| `docs/how-to/hair.md` | Improve hair with a comparison of Balanced, refiner off/on, tile budgets and matte levels; inspect alpha over black/white/checker backgrounds. |
| `docs/how-to/crop.md` | Remove off-screen room edges, keep full output bounds, handle moving subjects and automatic ROI; explicit preview may affect stream. |
| `docs/how-to/props-and-hints.md` | Keep chairs, microphones, held objects and second people; contrast chroma hint with optional portrait hints. CK1 requires a hint; CK2 defaults to none. |
| `docs/how-to/blue-screen.md` | CK1 blue checkpoint and matching despill/hint; current CK2/refiner validation limitations; no green/blue channel-swap workaround. |
| `docs/how-to/lip-sync.md` | Measured filter-added delay vs total camera/encoder delay, select audio, clap test, baseline/trim, manual-edit ownership and shared-microphone conflicts. |
| `docs/how-to/two-cameras.md` | Independent filters/state, shared downloads, GPU contention, per-source sizing, avoiding two controllers on one microphone. |
| `docs/how-to/offline.md` | Prepare known files, import and verify, acceptance, relocate cache, free disk, export diagnostics without network access. |
| `docs/performance.md` | Per-GPU class starting points, source/grid/tile costs, frame-age vs inference-time explanation, measuring while streaming, fair comparisons and reproducible reports. |
| `docs/compatibility.md` | Tested vs expected matrix, oldest API/distro floors, compatible newer versions, feature probes/fallbacks, known broken combinations and test evidence links. |
| `docs/amd-rocm.md` | User-facing subset of ROCM_AMD: Windows DirectML, Linux MIGraphX, exact community reports, install paths and honest hardware-test coverage. |
| `docs/troubleshooting.md` | Symptom → likely cause → smallest useful check → repair. Include full opacity, black input, colored fringe, frozen image, frame drops, no GPU backend, first build, bad hash, disk full and moved cache. |
| `docs/reporting-problems.md` | Copy diagnostics, inspect/redact log, open GitHub form, attach optional evidence and explain what maintainers need. No automatic uploads. |
| `docs/development/*` | Architecture, manifest/binding format, build/runtime ranges, shader/color contracts, tests, release process and contributor licensing boundaries. |
| `THIRD_PARTY_NOTICES.md` | Code/library and model scopes separately; publishers, licence IDs/text locations, export modifications and attribution. |

Installation guides must distinguish OBS 33's new Windows module layout from the older host layout and explain the portable instance's separate plugin directory. Show the actual package layout in the download instructions; do not tell users to move runtime DLLs blindly. Keep Linux paths tied to verified package/host discovery. [OBS plugin migration guidance](https://obsproject.com/kb/legacy-plugin-developer-info)

## Option reference contract

Treat the tables in [UX_DESIGN](UX_DESIGN.md) as the complete inventory. Maintain one settings schema in code from which keys, labels, units, allowed ranges, visibility, dependencies and default tables can be checked. Do not generate explanations from labels alone; write concise behavior notes and visual examples manually.

Each option entry contains: short UI label and stable key; plain meaning; effective range/units and default; which pipeline/profile uses it; what a larger/smaller value changes; quality/time/memory tradeoff; whether changing it resets state or rebuilds a graph; recommended first adjustment; one relevant screenshot or cross-reference. Derived/read-only fields explain what they measure. Retired Python/Spout/UDP settings live only in a migration appendix, never in first-run instructions.

Mandatory explanations often missed: H×W versus camera resolution; manual crop versus tracked ROI; black/white points versus contrast/choke; alpha coverage versus color; straight versus premultiplied output; semantic hint versus final alpha; two unrelated models called RVM; stage reuse versus output FPS; tile budget versus native resolution; larger band gain permitting more weight; exact licence versions; backend cache invalidation versus a corrupted model.

## Performance guide by GPU class

The following are **starting hypotheses**, not measured support claims. Publish actual results only with GPU, driver, OS/OBS, ORT/EP bundle, graph revision, source format/resolution/FPS, transport, profile, streaming workload and timing distribution.

| Class | First setup to try | What the guide must explain |
|---|---|---|
| NVIDIA RTX 30/40/50 class, adequate free VRAM | Auto backend, Balanced; try Facecam 60 if p95 misses target | TRT-RTX capability probe, first-run build, CUDA/TensorRT alternatives and contention with game/encoder |
| Older NVIDIA / limited VRAM | CUDA or DirectML if supported, Facecam grid, guided edges first | TRT-RTX has an architecture floor; a card can support CUDA but not this EP/model precision |
| AMD RX 9000 / RX 7000 supported variants | Windows DirectML; Linux matched MIGraphX; start Facecam or guided | Runtime graph support and native transfer matter more than “ROCm installed”; link community matrix |
| Older AMD and Intel DX12 | Windows DirectML, small grid, benchmark first | Device support does not guarantee live throughput; honest passthrough when unsuitable |
| Integrated/shared-memory graphics | Test backend, minimal profile | Shared memory may reduce copies but also shares bandwidth; do not promise dGPU performance |
| CPU | Diagnostic test only | Slow correctness path, no automatic real-time CPU fallback |

Always show the native C++/ORT results separately from the research PyTorch reference (11.1/15.6/30.4 ms without transfer). Use a repeatable 30-second warm test plus a representative streaming/game workload, report median/p95, processed FPS, displayed age, render lag and memory. State cold setup separately. Do not compare a 720p result to a 4K result without source dimensions. Leave unmeasured table cells empty; do not estimate FPS from GPU model names.

## Compatibility and issue reporting

Use matrix states: **expected**, **build-only**, **hardware-tested**, **community-tested**, **known-broken**, with date/report link and exact tested anchor inside the broader supported range. Newer unknown versions can run after feature tests; they get a diagnostic note, not a refusal solely because their version string is unfamiliar. A known-broken row names the failing feature and working fallback.

GitHub issue forms under `.github/ISSUE_TEMPLATE/`:

- **Bug / visual result:** expected and actual behavior, steps, frequency, first affected/last working plugin version, source type, filter order, source/canvas dimensions, color space and frame rate, model/profile/settings, number of instances, streaming/recording state, diagnostics and relevant log. An optional screenshot/clip must be the reporter's to share.
- **Backend / compatibility:** OS edition/distro/kernel/architecture, OBS install type/version and Qt version, GPU model/VRAM/adapter selection, driver, ORT and EP/runtime versions, selected vs actual backend, fallback path/reason, cold/warm failure phase, model/graph/cache identities, test-backend report, crash report if available.
- **Performance:** all environment fields plus source FPS/size, warm-up duration, sample duration, median/p95 phase times, processed FPS, render lag, other GPU workloads and comparison preset. The form should not require a heavy benchmark just to report a crash.

Provide a single **Copy diagnostics** report that fills the technical fields. Explain how to inspect it, then obtain OBS's current/last log through Help → Log Files, review it and attach only relevant material. Do not auto-upload logs or open a prefilled URL containing private text. Logs exclude tokens, footage, model bytes and personal paths by default; full OBS logs may still reveal device/source names, so user review matters.

Triage labels: `backend:cuda`, `backend:dml`, `backend:migraphx`, `transport`, `models`, `visual`, `sync`, `compatibility`, `needs-reproduction`, `community-tested`, `regression`. Maintainers reproduce with the fake pipeline first when capture/lifecycle is implicated, then smallest real graph. Add a report to the compatibility matrix only when its environment and outcome are sufficiently specified; do not promote every successful install to hardware-tested. Link duplicates and the known fallback; close resolved reports with the first fixed release and a retest request.

Public support destinations: [issue chooser](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose), [issues](https://github.com/MinersWin/CorridorKey-for-OBS/issues). Create files and links in implementation; this design does not claim templates already exist.

## Screenshot shot list

Capture **real OBS windows**, with a documented scene collection and representative green-screen footage. No generated UI screenshots or reconstructed controls. Use both Yami Dark and Yami Light where specified; keep the same footage/frame for comparisons. Record OBS/plugin/model versions, profile/settings, source size and screenshot scale in an asset index.

| ID / suggested filename | Required shot | Main annotation |
|---|---|---|
| S01 `add-filter-dark.png` | Camera Filters → add CorridorKey | The effect-filter location |
| S02 `first-run.png` | Default properties, Balanced, model missing, live camera | Models button and passthrough status |
| S03 `model-licences.png` | Real acceptance dialog with dependency list and readable terms link | Plugin licence vs model licences |
| S04 `download-progress.png` | Download with actual byte/rate data and cancel/resume | Destination and required space |
| S05 `first-build.png` | Preparing/building/warming phase | No false percentage; old output continues |
| S06 `ready-dark.png` | Default working pane and keyed scene | Preset, active backend and measured delay |
| S07 `ready-light.png` | Same pane in light theme | Readability/focus parity |
| S08 `advanced-groups.png` | Expanded Quality/Edge groups | Labels, units, Custom state |
| S09 `hair-comparison.png` | Same hair frame refiner off/on, real output captures | Fine detail without hiding artifacts |
| S10 `alpha-levels.png` | Alpha view with conservative vs excessive clipping | Lost transparency and black/white controls |
| S11 `despill-comparison.png` | Same green edge at 0 and .65 despill | Color correction and overcorrection |
| S12 `crop-preview.png` | Explicit crop inspection of room edges | Removed area vs retained source bounds |
| S13 `roi-debug.png` | Automatic ROI and user crop over moving subject | Different roles of the two boxes |
| S14 `hint-comparison.png` | CK1 chroma vs portrait hint with chair/prop/microphone | Person-only mask limitation |
| S15 `blue-checkpoint.png` | CK1 blue model selected, valid blue-screen output | Separate checkpoint/licence entry |
| S16 `tile-coverage.png` | Native tile debug at low/high budget | Selected band and unrefined regions |
| S17 `sync-controls.png` | Mic selection, measured/applied delay and trim | Opt-in ownership and units |
| S18 `audio-offset.png` | OBS advanced audio properties after auto sync | Actual offset, clap-test context |
| S19 `two-cameras.png` | Two sources/filters with different source resolutions | Independent state/shared model store |
| S20 `backend-test.png` | Real test result with backend/transport and timing | Tested combination vs expected |
| S21 `fallback.png` | Injected recoverable backend failure and passthrough | Error action and reason |
| S22 `offline-cache.png` | Import/cache usage/move UI | Original blobs vs bound/build data |
| S23 `copy-diagnostics.png` | Diagnostics preview before copy | Redaction and Report a problem |
| S24 `compatibility-warning.png` | Unknown version passes probe with advisory | Untested is not blocked |

Use native-resolution PNG captures and preserve enough OBS chrome to orient the reader. Crop distractions and omit decorative frames. Add numbered callouts outside controls, not over values. Supply alt text describing the task/result. Do not capture stream keys, accounts, personal scene names, Discord or unrelated windows. A fixture screenshot of an injected error must be labelled as such; performance screenshots must use real values.

Do not interrupt an active OBS session. Use a dedicated `CorridorKey Test` scene collection for captures. Verify older hosts with official portable builds in a separate test directory, isolated from installations used for streaming.

## Documentation acceptance

Before release, complete the first-run guide on a clean cache, follow offline import without network, reproduce one safe fallback and verify auto-sync restoration. Check every documented control against the settings schema and current UI, validate internal links and image captions, and check that both light/dark images are legible at normal documentation width. No unmeasured timing, unsupported licence claim, stale Python setup or unimplemented control may be presented as current behavior.
