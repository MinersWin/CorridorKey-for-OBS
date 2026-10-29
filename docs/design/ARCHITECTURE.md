<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: architecture

Design baseline: 28 October 2026. Status: implementation specification, not a claim of implemented or tested C++ behavior. See [contribution policy](../../CONTRIBUTING.md), [GPU-runtime linking permission](../../LICENSE-EXCEPTION.md), and [third-party notices](../../THIRD_PARTY_NOTICES.md) for licensing and distribution boundaries. Related specifications: [models](MODELS.md), [properties and dialogs](UX_DESIGN.md), [work packages](TASKS.md), [AMD support](ROCM_AMD.md).

## Decisions

- Build one native C++ video effect filter, `CorridorKey`, with an asynchronous inference worker and bounded frame ownership. Use OBS's supplied filter input; never call the target's video renderer recursively.
- Default to CK2 Balanced: Stage 1, a 512×512 subject ROI, v15 native-resolution refinement with 512 tiles. Offer Facecam 60 and Quality as explicit alternatives. The name Facecam 60 is a target, not a hardware-independent promise.
- Present a completed foreground and alpha with their original RGB frame. Do not silently apply an old matte to current video. Drop work when busy to keep latency bounded.
- Use GPU interop where verified; retain a staged small-grid transport as the portable baseline. The latter supports guided reconstruction, but native-detail refinement needs a separate tile or full-frame transport.
- Download graphs separately; obtain weights from their publishers and bind them locally in C++. No Python, PyTorch, model weights, or CK-derived graphs in the plugin distribution.

## Compatibility baseline

The current upstream stable is **OBS 33.0.0**, released 23 October 2026. Its release notes remove Ubuntu 24.04 and Qt 6.4-6.9 support and identify a `gs_image_file` breaking change. Therefore “OBS 31+/32+” is not a complete packaging specification. Use public libobs APIs, avoid private OBS/Qt classes and `gs_image_file`, and verify each supported host. [OBS 33 release](https://github.com/obsproject/obs-studio/releases/tag/33.0.0)

Recommended initial API floor: **OBS 30.2**, subject to the scaffold's public-symbol audit; target 30.2.x, 31.x, 32.x, 33.x and compatible newer hosts. Build against that oldest SDK, not automatically against the template's newer pin. Keep Ubuntu 24.04 with its available OBS 30-32 host lanes and add Ubuntu 26.04/OBS 33 packaging. Do not drop Ubuntu 24.04 to follow upstream. The current plugintemplate `buildspec.json` pins OBS 31.1.1: it is a starting point, not proof of compatibility. [Template](https://github.com/obsproject/obs-plugintemplate/blob/master/buildspec.json)

ORT upstream is **1.31.1**. Use reproducibly pinned build inputs, initially evaluating NVIDIA's documented ORT 1.30.0 / RTX EP 0.5 combination and AMD's validated 1.29.x bundle; evaluate 1.31.x separately, since EP readiness must be checked for each provider. These are test anchors, **not exact-version requirements for users**. The runtime abstraction targets ORT 1.23+ EP ABI features where available, with older factory APIs as capabilities permit. New core/provider contrib-op schema compatibility matters even if registration succeeds; do not mix arbitrary cores/provider libraries. Standard ONNX operators and an IR/opset supported by the selected bundle provide the widest range. [ORT 1.31.1](https://github.com/microsoft/onnxruntime/releases/tag/v1.31.1), [1.31.0 provider guidance](https://github.com/microsoft/onnxruntime/releases/tag/v1.31.0), [NVIDIA EP matrix](https://github.com/NVIDIA/TensorRT-RTX-EP-ABI#test-matrix)

OBS 33 also changes Windows plugin layout: the module now sits directly at `%ProgramData%/obs-studio/plugins/corridorkey/corridorkey.dll`, beside `data/`; pre-33 uses `bin/64bit/`. Package an explicitly tested layout for each host range, with upgrade/deduplication tests. Portable OBS 33 uses its own `plugins/` tree and ignores ProgramData. Linux layout guidance on the same page is unfinished, so verify distro paths independently of Windows rules. [OBS developer migration guide](https://obsproject.com/kb/legacy-plugin-developer-info)

### Compatibility matrix and runtime policy

This design baseline records no native plugin tests; see the [main README](../../README.md) for current implementation and validation status. “Expected” below is an engineering target from the public API requirements, not certification.

| Layer / range | Evidence now | Runtime decision |
|---|---|---|
| OBS 30.2.x-33.x, compatible newer | Expected C++ target; research prototype used installed OBS 33 | Query `obs_get_version`; resolve optional newer symbols with `GetProcAddress`/`dlsym`; use oldest public paths when absent |
| Windows 10/11 x64 on a host-supported release | Expected; local research measurements on Windows 11 only | Feature-test D3D11, adapter, sharing, D3D12 and EP; unsupported acceleration falls back |
| Ubuntu 24.04+, Ubuntu 26.04; other compatible Linux distributions | Packaging targets, not tested | Check ELF/glibc and Qt dependency floor at installation, then GL/EGL/device capabilities; source/tarball guidance for other distributions |
| Host Qt 6.x within binary-compatible range | Expected; OBS 33 builds use a newer Qt floor | Link the host-compatible Qt, never bundle a second Qt; separate distro artifacts where required |
| ORT 1.23+ with supported EP ABI, or compatible legacy factory | Expected interface range; anchors AMD 1.29.x, NVIDIA 1.30.x; evaluate newer 1.31.x | Negotiate C API version; enable only APIs/providers actually present; graph IR/opset must be accepted |
| TensorRT-RTX / TensorRT / CUDA / cuDNN versions accepted by their matched EP | Vendor-dependent range, not a universal numeric range | Query versions/device/driver, create session and probe; fail that backend only on real incompatibility |
| NVIDIA drivers satisfying the chosen runtime's documented minimum; newer compatible drivers | No driver sweep performed | Check vendor capability/compatibility, not version equality; older GPUs/drivers try CUDA, DML or readback |
| ROCm/MIGraphX supported pairs, starting with maintained 7.2.x and 10.x lanes | Vendor matrices only; no local AMD hardware | Register the compatible EP, probe graph and interop; a failed HIP path uses staged transfer, not a plugin-load failure |

Unknown/newer versions **load and probe**, with a warning saying the combination is untested. Version ranges inform recommendations and cache identity; they are not arbitrary upper bounds. A genuine missing required symbol, incompatible C/Qt ABI, unsupported instruction set or vendor-documented impossible combination may disable a component. Explain the exact reason. If the module cannot load safely, log it; if it can load, show passthrough status. A single unavailable EP must never prevent the filter or model manager from loading.

Maintain a versioned public matrix with `expected`, `build-only`, `hardware-tested`, `community-tested`, and `known-broken` cells, exact test anchors, date, issue/report link and tested transport/model/profile. Never label an AMD cell community-tested before a community run exists. Test the oldest supported host plus recent stable and one newer/unknown simulated version; optional-symbol absence must exercise a working fallback. Build pins and artifact hashes remain exact for reproducibility; user support is capability-based.

Concrete NVIDIA starting ranges, verified from vendor documentation but **not tested with this plugin**: RTX EP 0.5 lists ORT 1.25.0-1.30.0 and TensorRT-RTX 1.7.x.x. Its CUDA 13 package requires drivers at least 590.48.01 on Linux or 580.88 on Windows; a separately source-built CUDA 12.9 package lists 575.57.08 / 576.57. Keep matched libraries together and apply the actual release's bounds; newer negotiated cores/drivers remain probe candidates. Users need the driver and packaged runtime, not a CUDA development toolkit. [EP matrix](https://github.com/NVIDIA/TensorRT-RTX-EP-ABI#test-matrix), [runtime prerequisites](https://nvidia.github.io/TensorRT-RTX-EP-ABI/prerequisites.html)

For the CUDA fallback, ORT documents a CUDA 13 family and separate CUDA 12.8+ builds, with cuDNN 9.x; cuDNN 8/9 are not interchangeable. Preserve a CUDA 12 transport/runtime lane where its hardware and EP remain supported, instead of forcing every GPU onto the newest toolkit family. Provider-bundle metadata owns these requirements; a blanket `CUDA >= 12` test is insufficient. [CUDA EP requirements](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html#requirements)

### Support reports

Add **Copy diagnostics** and **Report a problem** to the model manager. The latter opens the public GitHub issue chooser only; it does not submit or upload anything. Copy diagnostics includes plugin/build ID, OBS/OS/distro/kernel/Qt, GPU/VRAM and adapter match, driver, ORT/EP/runtime versions, detected features and selected fallback, graph/package hashes, profile/grid/source size/color space/FPS, instance count, median/p95 phase timings, cache state and recent plugin error codes. Exclude frames, audio, model data, tokens, source names and personal paths by default. Let users inspect the text and optionally attach the OBS log after review.

The issue form requests expected/actual result, reproduction steps, first affected/last working version, whether streaming/recording, selected model/profile/backend/transport, Copy diagnostics output, relevant OBS log or crash report, and a minimal scene description. Provide a checkbox confirming private data was reviewed; screenshots/clips are optional and require permission to share. Public reports go to [GitHub issues](https://github.com/MinersWin/CorridorKey-for-OBS/issues). See [documentation plan](DOCS_PLAN.md) for triage and matrix updates.

## Modules and execution contexts

These proposed module paths may change during implementation; preserve their interfaces.

| Module | Responsibility | Execution context |
|---|---|---|
| `src/plugin.*`, `src/obs/filter.*` | Registration, lifetime, source dimensions, OBS callbacks | OBS callbacks; no inference |
| `src/obs/filter_input.*` | Consume filter input once per video tick, preserve graphics state | OBS render |
| `src/obs/properties.*`, `src/settings/*` | Properties, validation, migration, immutable settings snapshots | UI/control |
| `src/runtime/frame_pool.*` | Bounded slots, sequence/generation IDs, timestamps, release tokens | Render/worker via atomics |
| `src/runtime/scheduler.*` | Fair per-device admission, newest pending frame, cancellation | Inference scheduler |
| `src/engine/pipeline.*`, `ck2_pipeline.*`, `ck1_pipeline.*`, `fake_pipeline.*` | Typed graph stages, state, frame transformations | Inference worker |
| `src/engine/ort_session.*`, `providers/*` | Device selection, sessions, I/O binding, cache compatibility | Loader/worker |
| `src/gpu/{d3d11_cuda,d3d12_dml,gl_cuda,gl_hip,staged_readback}.*` | Resource sharing, format conversion, nonblocking completion | Render half + worker half |
| `src/gpu/post/*`, `data/effects/*` | Guided coefficients, tile operations, despill and composition | GPU; bounded render submissions |
| `src/models/*` | Manifest, licences, downloads, binder, cache and locks | Background I/O/build |
| `src/ui/model_manager.*`, `backend_test.*` | Qt dialog and probe results | Qt GUI |
| `src/telemetry/*`, `src/obs/audio_sync.*` | Rolling timing, status, optional offset ownership | Worker samples; control applies |
| `tools/backend_probe/*` | Disposable native process to test loading/running a provider | Separate process |

Only immutable model bytes, validated graph recipes, provider inventory and download/cache services are shared globally. Each filter owns its crop, temporal state, frame slots, active generation and settings. Two cameras must never share recurrent tensors or a “last alpha”. A shared scheduler may serialize inference without sharing those states.

## Threading and frame lifecycle

**Render callback:** read the current settings/result snapshots, check dimensions, acquire an already-free slot, enqueue GPU capture/preprocessing, and draw the latest completed paired result. If no slot is free, skip submission. No network, hashing, disk I/O, model creation, condition-variable wait, blocking map, device synchronize or worker join occurs here.

`obs_source_process_filter_begin[_with_color_space]` and a capture effect's technique provide the input. Disable direct rendering when a materialized input is required; finish the matching begin/end pair and restore render target, viewport, projection, blend and sRGB state. A texrender owned by the filter can receive that pass. The effect's image input is the filter-chain input, including earlier filters. Test image, browser, media, capture-card and camera sources; a camera-only test misses the known recursive-render failure. [Source API](https://docs.obsproject.com/reference-sources)

**Worker:** wait outside OBS graphics ownership for an input fence, pack tensors, run stages and postprocessing, signal completion, and publish a result descriptor. GPU resources remain owned until both producer and consumer finish. `Run()` may block this worker; it must not hold OBS graphics locks while doing so. Use a dedicated native device/context where necessary; never access OBS's D3D11 immediate context from the worker.

**Loader/build task:** downloads and prepares a replacement pipeline, compiles and warms it, then requests a generation swap. Keep the old pipeline live until replacement is ready; on a failed replacement retain the old one. Debounce graph-changing settings. Cancel cooperatively between stages; an uninterruptible driver build is shown as “Stopping after this step,” not falsely reported as cancelled.

**UI/control:** use queued Qt signals for progress and status. Do not call Qt from render or worker callbacks. Hot slider changes publish small parameter blocks; graph/grid/provider changes create a new generation. Destruction first detaches callbacks, cancels pending work, then retires graphics objects on the graphics thread after completion. Join threads only from a safe teardown/control context.

Start with **three native frame slots per instance**, plus at most one pending input descriptor; increase only when measured. State transitions are `free → capturing → ready → processing → complete → presenting → retired → free`. The presenting slot remains pinned. Never overwrite an in-flight slot. Deduplicate by OBS video tick plus instance generation so preview, program and duplicate scene draws do not advance the network twice. For synchronous sources there may be no upstream capture sequence; do not pretend content hashing gives a camera timestamp.

## GPU data paths and fallback chains

“GPU interop” means no CPU pixel round-trip. Copies, format conversion and synchronization still cost time; do not market it as zero latency.

### Windows / NVIDIA

Decision: **D3D11 → CUDA interop**, shared adapter selected by DXGI LUID, then CUDA memory bound to the tested TensorRT-RTX/CUDA runtime. Copy OBS's input into plugin-owned textures. Register compatible private textures once with `cudaGraphicsD3D11RegisterResource`; map/unmap and pack BGRA/RGBA to NCHW on the worker after a completion signal. CUDA texture arrays are not ORT tensor pointers: a CUDA kernel must populate linear device buffers. Reverse the operation for results. [CUDA graphics and external interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)

For a separate D3D11 worker device, use supported shareable formats and NT handles/fences or keyed mutexes. A shared resource must pass the actual CUDA registration probe; if registration of a shared texture is unsupported, copy on the worker device to an ordinary registered texture. Render-side acquire is a zero-timeout try; keep the old result if unavailable. Never reproduce the prototype's unguarded shared texture access or assume shared handles fit in 32 bits.

Fallback order: validated direct/private-texture interop → explicit external-memory/shared synchronization implementation if supported → staged analysis/tile readback → optional staged full-frame readback → passthrough if the latency or capability limit fails. Changing transport does not require changing EP. An adapter mismatch starts on readback; cross-adapter sharing is not a first-release promise.

### Windows / AMD, Intel, and NVIDIA fallback

Decision: **DirectML on D3D12**, initially with staged transport; promote D3D11/D3D12 sharing only after the synchronization spike passes. Match the D3D12 adapter to OBS. Capture into a compatible D3D11 shared resource, open on D3D12, fence the ownership handoff, then compute-convert texture pixels into D3D12 tensor buffers. Use `SessionOptionsAppendExecutionProvider_DML1` with our queue and device. Wrap buffers using the supported `OrtDmlApi` resource allocation interface; I/O binding alone does not turn a D3D11 texture into a tensor. [DML device tensors](https://onnxruntime.ai/docs/performance/device-tensor.html)

D3D11-on-12 is not a drop-in replacement for OBS's existing device. Use it only in a proven private-device bridge, never by replacing the host renderer. Fallback: shared D3D11/D3D12 resources → staged grid and optional tile atlas → staged full frame if explicitly requested and fast enough → passthrough. DML sessions use sequential execution, disabled memory patterns and no concurrent `Run` on one session. [DML requirements](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)

### Linux / NVIDIA and AMD

Decision: use **OpenGL texture/PBO interop with CUDA** for NVIDIA and **HIP interop feeding MIGraphX** for supported AMD systems, subject to runtime probes. EGL is the context/window-system mechanism, not an inference EP. The bridge must work with the actual OBS EGL or GLX context and matching device; do not assume CUDA EGLStream APIs are available just because OBS uses EGL.

Copy the filter input into a plugin-owned GL object, fence it, and register a compatible texture or buffer. If a worker GL context is needed, create an explicitly shared context without moving OBS's context. Keep all host `gs_*` calls on their proper thread. CUDA/HIP kernels convert mapped arrays/PBOs into linear tensors. Try texture interop, then PBO interop, then a staged PBO/readback ring. On output reverse the path or upload completed small maps/tile results. Unsupported EGL/driver/context combinations use readback. [HIP OpenGL interop](https://rocmdocs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/opengl_interop.html)

### Staged readback and the refiner

Capture only when capacity exists. At tick N, issue the downsample and copy. At N+1 or later, poll readiness; **one frame of delay is not proof that a map cannot block**. Use D3D11 event queries and `DO_NOT_WAIT`, or GL fences/PBOs with zero-timeout checks. The public `gs_stagesurface_map` call alone does not establish a nonblocking guarantee. If a platform cannot prove readiness, skip the sample or use an appropriate worker-owned staging context. Upload from a bounded result queue.

| Transport payload | Example uncompressed input | Appropriate use |
|---|---:|---|
| 256×512 BGRA grid | 0.50 MiB/frame | Facecam inference with guided output; retain native RGB on GPU |
| 512×512 BGRA grid | 1.00 MiB/frame | Balanced CK inference without native CNN refinement |
| 3840×2160 BGRA frame | 31.64 MiB/frame; 1.85 GiB/s at 60 samples/s, one direction | Diagnostic fallback; benchmark contention and upload separately |
| 512 native 52×52 RGB tiles, fp16 | 7.92 MiB/frame before other fields | Sparse refiner transport; more than a low-res grid, but less than full native feature tensors |

Decision: portable baseline reconstructs full-resolution output from small maps and the retained native frame. For refinement without interop, generate a **native RGB tile atlas** on the OBS GPU after the frame's band/indices return, stage it, and run the refiner on the worker. This adds another pipeline phase and frame retention; show the measured delay. Transfer only native RGB where possible and derive the other refiner channels on the inference device. Do not read back eleven full-resolution channels. Offer “Guided edges” as the explicit performance fallback, with a status notice that refinement is off. Never pass an enlarged inference grid to the refiner and call it native detail.

## Execution-provider selection and caches

Auto selection checks device and model capabilities; a vendor name alone is insufficient:

1. Windows NVIDIA Ampere or newer: tested TensorRT-RTX plugin; then installed/tested conventional TensorRT + CUDA; then CUDA; then DirectML.
2. Linux NVIDIA: the same NVIDIA chain where packaged. No DirectML Linux fallback.
3. Windows AMD/Intel: DirectML. Native Windows MIGraphX stays disabled until a supported package and our parity/interop tests exist.
4. Linux AMD: matched MIGraphX/ROCm bundle; otherwise passthrough. CPU is an explicit diagnostic option, never an automatic live fallback.

Probe availability, driver, adapter, precision, operators and memory; then perform a deterministic tiny run. Cache the result by runtime/driver/device identity. A provider can exist and still fail this model. Reject large unexpected CPU partitions for live use; report the unsupported graph and offer another tested backend. Do not mix a DML tensor allocation with a CUDA session. [Provider catalogue](https://onnxruntime.ai/docs/execution-providers/)

Use the standalone TensorRT-RTX EP ABI, not its deprecated built-in implementation. Cache compiled EP context artifacts and the GPU-specific **runtime/JIT cache** (`nv_runtime_cache_path`). Conventional TensorRT separately supports engine/profile caches and timing caches; these are not interchangeable with the RTX cache. Never invent a `trt_*` option for TensorRT-RTX. [RTX provider](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html), [conventional TensorRT caches](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)

Cache identity includes manifest and graph hashes, all weight hashes, binding recipe version, ORT and EP build IDs, driver, GPU UUID/architecture, precision, shapes, tile batch and provider options. Keep compiled artifacts local, validate before reuse, write atomically, and lock across two filter instances/OBS processes. A bad cache is evicted and rebuilt once; repeated failure disables the backend for that run. Do not expose “force incompatible timing cache” in ordinary UI.

Build only the selected profile first. UI phases: downloading → verifying → preparing weights → building backend → warming up → ready. Compilation progress is phase-based with an indeterminate bar unless the API provides real progress. Say “First setup can take several minutes”; do not promise timings from unrelated models. During builds retain the old working pipeline or passthrough. Do not start an expensive optimization build automatically while streaming/recording; an explicit Test/Optimize action can start it.

## Pipeline contract

Use a small C++ interface around `prepare`, `reset`, `submit`, `poll_result`, `capabilities`, and `diagnostics`; method names are illustrative and do not define an ABI. Frame descriptors carry instance ID, sequence, source size, color space, crop transform, generation, origin time and device/host storage. Result descriptors carry the same identity, straight foreground/alpha or paired output, timings and a completion token.

**CK2:** MoGe encoder → recurrent video MAE step → Stage 1 auxiliary output/baton → optional Stage 2 → shared reconstruction/refiner. Exact tensor names, layout, color transforms and output meaning come from the export contract, not assumptions in plugin code. Static graphs exist per H×W grid. RVM here means Google DeepMind's Recurrent Video MAE, not PeterL1n's Robust Video Matting.

- RVM state: `(1, 1 + h64*w64, 384)`, private to each sequence. MoGe feature cache: `(1, 768, h64, w64)`; `h64=H/64`, `w64=W/64`.
- Refresh MoGe every 3 accepted frames, earlier when mean absolute difference of a 32×32 thumbnail exceeds 0.05. Reduction/readback runs off the render thread. This motion trigger is not by itself a calibrated scene-cut detector.
- Compute RVM features each accepted frame. At a gap over 250 ms, explicit reset, source replacement, grid/crop/color-space change or seek, clear temporal state. A large thumbnail discontinuity may conservatively reset; threshold requires parity/evaluation before enabling a separate cut detector.
- ROI proposal every 4 processed frames: quarter-resolution alpha >0.1 bounding box, 20% padding, minimum side 0.35, EMA 0.8, clamped inside user crop. Empty alpha returns toward the user crop/full frame. Snapshot the transform with each result. Reset state if the sampling transform changes; test this behavior against the exporter reference.
- Stage 2 interval 0 means auxiliary only. Residual reuse and staggering are expert experiments, disabled in presets. No C-RADIO stage is shipped.

**CK1:** hint source → GreenFormer → shared reconstruction. Revision 4 takes sRGB RGB in [0,1] plus a linear coarse alpha hint; normalization is inside the graph. CK1 has no hint-confidence scalar. The owner decision makes MODNet the default and keeps chroma Expert-only, with a teal/uneven-screen warning. CK2 Stage-1 alpha is also available but loads the CK2 foundations and adds their cost. Explain that person hints can omit props or chairs. CK1 is Advanced-only and Experimental; recommend CK2 Facecam without refiner for weak GPUs. Learned refinement stays off until an evaluated CK1 package exists. See [verified contract](MODELS.md).

**Refiner:** optional graph with fixed batch K and 52×52 tiles for v15: 32×32 valid center plus 10-pixel halo. Its 11 semantic channels are native sRGB I(3), estimated background B(3), transferred foreground F(3), network alpha(1), guided alpha(1). Internal channel padding to 16 belongs in the graph contract. v15's four dilations are 1,2,4,1; do not derive the halo from the Python class's different constructor defaults. Dummy slots are masked; duplicate padded tiles never write output. Learned refinement is disabled for CK1.

## Shader / GPU pass list

This is a logical pass list. Fuse bandwidth-heavy pointwise work after parity is established. Portable raster passes use OBS effects; top-k/gather/scatter may use private CUDA/HIP/D3D12 compute. OBS's effects API is not a universal compute-shader abstraction.

| Pass | Inputs | Outputs / invariant |
|---|---|---|
| P0 Capture | OBS filter input, source color metadata | Retained native frame, source alpha, sequence/time; no extra target render |
| P1 Crop/resize | P0, user crop, ROI transform | Static grid RGB, 32×32 thumbnail; antialiased MoGe bicubic resize as separately verified operation |
| P2 Hint | Grid RGB or selected hint model output; optional prior state | Linear coarse mask; align to the exact grid/crop |
| N1 Neural stages | Grid/hint, cache, state | Low-res straight foreground, alpha, next state, optional baton/residual |
| P3 Decode/align | Graph outputs and manifest color contract | sRGB low-res foreground and guide RGB; network alpha; no copied CK color constants |
| P4 Guided moments | Guide RGB, alpha, radius, epsilon | Box-filtered means/covariances, FP32 3×3 solve and smoothed four coefficients |
| P5 Color/background | Low RGB, low FG, alpha | FG delta; background from linear-light weighted push-pull pyramid, returned to sRGB |
| P6 Band/budget | Low alpha, threshold, gain, source size | Dilated/feathered uncertainty, deterministic top-K tile IDs, selection-neighbor mask |
| P7 Base reconstruction | Native RGB, coefficients, delta | Native guided alpha and straight FG; `align_corners=false` pixel-center sampling |
| P8 Tile gather | Native P0 frame, P4/P5/P6 | K×11×52×52 FP16 tile tensor, un-despilled; border clamp and valid-slot mask |
| N2 Refiner | P8 | Refined tile alpha and straight FG |
| P9 Tile blend/scatter | N2 centers, base fields, band/neighbor mask | Native raw alpha/FG, feathered toward unrefined neighbors |
| P10 Finish | P7 or P9, contrast/offset, despill, black/white levels | Finished alpha/FG; despill once after refinement, then levels and crop/source-alpha mask |
| P11 Output | P10, retained input, selected view | Premultiplied OBS output or explicit debug view; match host working color space |
| P12 Reductions | Alpha/thumbnail, source transform | Next ROI/motion metadata; no blocking render-thread read |

For default delta reconstruction, transfer low-frequency FG correction onto native RGB; keep the guide solve in FP32 to avoid cancellation. The alternative compositing-equation unmix is expert-only and needs an epsilon guard at tiny alpha. Average-green despill is applied in the sRGB-domain reference path after refinement; older docstrings calling this entire path scene-linear are misleading. Linear conversion is required for background estimation and correct OBS composition, not blindly for every formula. Independently implement standard sRGB conversion; publisher-specific transforms remain in the graph/manifest contract.

The refiner band uses the clamped product of gain and nearby alpha. **Larger positive gain increases the allowed band weight**; the Lua tooltip claiming “higher is stricter” contradicts the implementation. Preserve the formula and correct the tooltip. Guidance, contrast, levels and source alpha must not be applied twice when P7/P10 are fused.

CK1 Blue uses its own checkpoint, chroma-hint configuration and separately evaluated blue-spill finishing variant. Do not apply the green-only P10 formula unchanged, swap RGB channels as a model substitute, or claim the CK2-trained refiner is blue-screen validated.

First release accepts SDR sRGB/Rec.709 with explicit conversions and premultiplication tests. HDR sources require a verified scene-linear/model transform; until then pass through with “HDR input is not supported by this model.” Do not clamp HDR silently.

## Pacing, latency and audio sync

Use a per-device fair round-robin scheduler with one inference job admitted at a time initially. Two independent model sessions can run concurrently only after measurements demonstrate benefit and memory headroom. A pending newest frame replaces an older pending frame; running work completes. Repeated presentation never updates recurrent state. When all slots are held, skip capture. Hide/show pauses scheduling and resets after a long gap.

Measure `present_time - captured_filter_input_time` for the exact frame displayed, using one monotonic clock. This is **filter-added age**, not sensor exposure-to-screen or encoded-stream delay. Record capture/queue, input transfer, neural stages, refiner, output transfer and presentation separately. Show rolling median and p95, processed FPS, dropped submissions and repeated presentations. GPU timings come from asynchronous event/query results, never a blocking timing query in render.

A stale completed result may be repeated up to 250 ms. Beyond that choose live passthrough and a visible error; never leave an indefinitely frozen camera. A first result starts paired output. Stable fixed-delay playout is optional later; variable age is unavoidable when inference misses deadlines and must be shown honestly.

Auto lip-sync is off by default and needs an explicitly selected audio source. Store its baseline nanosecond offset; apply `baseline + filtered filter_delay + user_trim`, never repeatedly add to the current offset. Update on the control thread at most once per second after a 2-second stable sample window, with 5 ms hysteresis and a 10 ms/s slew limit. Cap added delay at the configured limit (default 250 ms). Display the actual applied value. On bypass/failure/disabling, restore only if the current offset still equals our last write; a manual edit relinquishes ownership. Two filters cannot both own one microphone. Camera timestamps, capture latency and external microphone delay still require a clap test. [Sync-offset API](https://docs.obsproject.com/reference-sources#c.obs_source_set_sync_offset)

## Size changes and failures

Native allocations always follow **filter input dimensions**, not canvas/output resolution or a 4K constant. A change creates a generation; old results are rejected, resources retire after fences, temporal state resets, and graph grid stays at its profile size. Portrait, odd dimensions, zero-size/no-signal and rapid reconnects need tests. Source crop reduces the processed region but preserves the full source output bounds with transparent pixels outside it.

| Failure | Required behavior |
|---|---|
| Missing model / licence not accepted | Live passthrough; Open Models action |
| Download interruption / disk full / hash mismatch | Keep verified installation intact; resumable error; never load partial files |
| Refiner unavailable | Guided reconstruction with explicit status; retain core pipeline |
| EP unsupported / compile failure | Try the next backend that passes capability and graph probes once; show the reason and actual backend |
| OOM | Retire failed generation; suggest smaller profile; no endless reload loop or hidden model change |
| Device lost / interop timeout | Stop submissions, retire safely, passthrough; one controlled reprobe outside render |
| Invalid/nonfinite graph output | Discard result; reset state; disable pipeline after repeated failures |
| Source deleted / teardown | Weak references, cancellation, deferred resource retirement; no UI callback into freed data |

No C++ exception may cross an OBS C callback. Every error has a stable code, short action text and detailed log entry with paths redacted where appropriate. “Never crash OBS” is the safety target, but in-process native drivers can still crash or hang beyond C++ exception handling. Test backend loads in a separate native probe process; do not claim this isolates every live inference failure. If absolute inference-process crash isolation is mandatory, a dedicated runtime host and Linux cross-process transport are a separate architectural work package, not a hidden promise.

## Evidence and release gates

The research results after acceleration steps 1-7 are 11.1/15.6/30.4 ms for Facecam/Balanced/Quality on RTX 5070 Ti, without D3D11 transfer, averaged across mixed source sizes. They include research-only model/kernel optimizations and are **not ORT forecasts**. v12 improves edge/temporal metrics over v10w across most reviewed sets; head passes cost 20-30 ms in the reported experiment. Source: research `docs/EVAL_RESULTS.md`, rounds 11/12 and acceleration section; `HANDOVER_STATE.md` section 0.

Lessons from the Rust CK1 project are filter-provided input, deferred staging, static graphs and rank-four attention for its DirectML export. These are design observations, not copied implementation. Its throughput and low-resolution matte strategy do not establish CK2 feasibility. [tangentstorm design notes](https://github.com/tangentstorm/corridorkey-obs#how-it-works)

Release gates: synthetic/fake pipeline passes all lifecycle tests; official graph parity and color contract delivered; each advertised EP has a real model smoke test; native-tile parity including halos and edge seams passes; a two-camera run and resize/reset fault matrix passes; p95 render-thread overhead and end-to-end delay are measured under streaming load. Build-only AMD CI is reported as build-only.
