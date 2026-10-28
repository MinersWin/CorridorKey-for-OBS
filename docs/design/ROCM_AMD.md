<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: AMD and ROCm assessment

Verified against online primary sources on **28 October 2026**. This is a capability and implementation assessment. No AMD hardware was available and no AMD inference or interop test was run. “Community-tested” is a future evidence label, not the present status of every AMD path.

## Decision

Use **DirectML on native Windows** as the AMD baseline. Use **MIGraphX on Linux** as the AMD acceleration path, with staged transfer first and HIP/OpenGL interop after verification. Keep an abstraction slot for a future supported Windows MIGraphX package; do not tell users that installing HIP makes that backend available today.

The old **ROCm Execution Provider** is not the modern fallback: ORT removed it in 1.23 and directs users to MIGraphX. AMD's last supported distribution of the old provider was ROCm 7.0; 7.1+ removes it. Distinguish the ROCm platform from an execution provider named ROCm. [ORT removal notice](https://onnxruntime.ai/docs/execution-providers/ROCm-ExecutionProvider.html)

## Current versions and conflicting documentation

The current AMD unified release is **ROCm Core SDK 10.1.0**, dated 5 October 2026. Its ecosystem table lists **ORT 1.29.0 and MIGraphX 2.18 on Linux**; its component table includes **HIP 7.16.0** on Linux/Windows. Windows compute support has expanded, but that does not make every framework available there. [AMD 10.1 release notes](https://rocm.docs.amd.com/en/latest/about/release-notes.html)

ORT's MIGraphX provider page still displays the older ORT 1.23.2 / ROCm 7.2.1 combinations. The separate Radeon/Ryzen documentation explicitly ends at 7.2.1 and points toward unified documentation. These older pages are valid historical lanes, not the latest universal matrix. Use the AMD unified release and ecosystem installation page for the current lane, retaining older combinations only where tested. [ORT MIGraphX page](https://onnxruntime.ai/docs/execution-providers/MIGraphX-ExecutionProvider.html), [legacy Radeon matrix](https://rocm.docs.amd.com/projects/radeon-ryzen/en/latest/docs/compatibility/compatibilityrad/native_linux/native_linux_compatibility.html)

AMD's current installation guide describes the **MIGraphX plugin EP** and explicit provider registration; it also documents packaging/library-path workarounds in its Python examples. Those examples establish provider availability, not a requirement to install Python for this C++ plugin. Package or build the native compatible libraries and initialize them through the C API. [AMD ORT installation](https://rocm.docs.amd.com/projects/ai-ecosystem/en/latest/inference/onnxruntime.html)

ORT upstream itself is 1.31.1. Do not combine that newest core arbitrarily with AMD's 1.29.0-tested plugin. Pin reproducible build inputs, negotiate compatible runtime interfaces, and broaden the supported range when real probes/tests pass. Newer unknown versions are not rejected merely for being newer. [ORT release](https://github.com/microsoft/onnxruntime/releases/tag/v1.31.1), [AMD compatibility matrix](https://rocm.docs.amd.com/en/latest/compatibility/compatibility-matrix.html)

## Windows

| Path | Verified scope | Product position |
|---|---|---|
| ORT DirectML, native Windows | D3D12-capable AMD hardware is supported by the EP; actual graph/operator support still matters | First AMD backend; use explicit device/queue and tested graph variants |
| HIP / ROCm Windows components | Current AMD core components support Windows for listed hardware; legacy HIP SDK and newer Core SDK naming must not be conflated | Useful runtime capability, insufficient evidence for Windows ORT/MIGraphX |
| ORT MIGraphX, native Windows | No supported native Windows package/framework lane verified; current AMD ecosystem table says Linux | Experimental slot only; absent in normal backend selection until independently validated |
| WSL ROCm | A Linux environment with separate graphics/device constraints | Not a native Windows OBS interop solution; do not route production video through WSL |

DirectML is in sustained engineering; new Windows deployment work is moving toward WinML. The existing DML EP remains supported and offers the explicit queue/device control needed here. Retain it now; assess WinML as a later packaging/provider-discovery option without adding a second deployment path before the current one works. [ORT DirectML status and requirements](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)

For DML, set sequential session execution and disable memory-pattern optimization; serialize `Run` per session. Use the actual OBS adapter, not “GPU 0”. Prefer static shapes and the exporter's rank-four attention variant; successful session creation is insufficient, so run an attention test and the actual model. A CPU-partitioned graph may technically run while missing live deadlines: surface this explicitly.

**Interop plan:** OBS renders through D3D11. DirectML consumes D3D12 buffers through its device allocation API. Share/copy a compatible texture between D3D11 and D3D12 on the same adapter, synchronize ownership, and convert into tensor buffers with compute. D3D11/D3D12 sharing requires an explicit bridge implementation. Start with staged grid/tile transfer; enable sharing only after the probe and long-run test pass. [ORT device tensors](https://onnxruntime.ai/docs/performance/device-tensor.html)

Expected performance class: a capable discrete Radeon is a plausible live candidate for small-grid CK1/CK2; exact quality/FPS and the full CK2 transformer operator coverage are **unmeasured**. Driver compilation, attention lowering and transfers may dominate. Older DX12-compatible cards can pass the API requirement yet be too slow or memory-limited for these models. Do not publish NVIDIA-to-AMD FPS conversions.

## Linux

Decision: support a **matched MIGraphX/ROCm combination**, not the removed ROCm EP. Begin with the modern native plugin lane and retain a known older factory-based lane only when there is a supported artifact and meaningful demand. The user-facing backend name is MIGraphX; diagnostic details show ORT, MIGraphX, HIP, driver and distro versions separately.

The current AMD ORT page lists Radeon architectures **gfx1200, gfx1201, gfx1100, gfx1101 and gfx1102**. Representative devices include RX 9070/9070 XT (gfx1201), RX 9060-series (gfx1200), RX 7900-series (gfx1100), RX 7800 XT/7700-series (gfx1101) and RX 7600 (gfx1102); compatible PRO variants are also listed by AMD. An architecture listing does not certify every board, OS and driver combination. Check the device row and framework row together. [AMD ORT prerequisites](https://rocm.docs.amd.com/projects/ai-ecosystem/en/latest/inference/onnxruntime.html), [AMD hardware table](https://rocm.docs.amd.com/en/latest/about/release-notes.html#amd-hardware-support)

Do not mark RX 6000/5000/Vega generally supported by this ORT path from anecdotal HIP success or an architecture override. Older PRO boards may appear in the Core SDK hardware table without appearing in the ORT framework list. `HSA_OVERRIDE_GFX_VERSION` is not a supported installation step. Integrated Ryzen targets need their own framework/driver validation and memory accounting.

Ubuntu 24.04 remains our first distro target; newer Ubuntu is an additional lane. Kernel/HWE, amdgpu driver and user-space libraries must be a supported set. OBS 33 no longer supplies the old Ubuntu 24.04 lane, so retain an appropriate older OBS package there without requiring an OS upgrade to use the filter. Other distributions are “expected/source build” until their dependencies and runtime tests are recorded. [OBS 33 release notes](https://github.com/obsproject/obs-studio/releases/tag/33.0.0), [AMD OS/framework matrix](https://rocm.docs.amd.com/en/latest/compatibility/compatibility-matrix.html)

**Interop plan:** register plugin-owned GL textures or PBOs through HIP graphics interop, map after fences, pack tensors on the GPU and bind supported device allocations to MIGraphX. ORT's MIGraphX memory/stream interop for the selected plugin bundle requires a native spike; CUDA allocation names cannot simply be reused. A worker may need a shared GL context; it must not migrate OBS's context. [HIP OpenGL API](https://rocmdocs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/opengl_interop.html)

HIP API documentation describes GL resource registration, but it does not establish successful interop with every Mesa/EGL/GLX/Wayland combination. EGL availability is not CUDA EGLStream compatibility. Test actual renderer, device identity, registration, fence completion and round-trip pixels. Fallback chain: GL texture interop → GL buffer/PBO interop → staged small-grid transfer → optional staged native tile atlas → passthrough if no working inference backend. Readback is a **transport fallback**, not an alternative to MIGraphX inference.

Expected performance class: modern RDNA3/4 cards may be suitable for small static-grid keying with FP16, while compiler partitioning and matrix kernels determine whether Balanced/Quality are practical. This is a hypothesis. Initial memory targets and tile budgets come from tests, not nominal TFLOPS. CPU remains a manual diagnostic option. Refiner needs native pixels even when inference is low resolution; measure the additional tile phase explicitly.

## Compatibility matrix: evidence versus expectation

| Configuration range | Current evidence | Initial policy / planned anchor |
|---|---|---|
| Windows 10/11 + supported DX12 Radeon + DML | Vendor capability documented; no AMD CK run | Expected, start with staged transport; test actual source/model |
| Native Windows + ROCm/HIP + MIGraphX | HIP exists; supported Windows ORT package not verified | Do not advertise; keep optional capability slot |
| Linux + supported RDNA3/4 + matched ROCm 10.x/MIGraphX | Vendor current framework lane | Expected; first modern anchor ORT 1.29.x, MIGraphX 2.18, ROCm 10.1 |
| Linux + supported Radeon + ROCm 7.2.x/MIGraphX | Historical vendor matrix | Possible compatibility lane; anchor ORT 1.23.2/ROCm 7.2.1, not automatically certified |
| Linux HIP GL/EGL interop | API exists; OBS integration untested | Experimental until hardware round-trip, resize and soak tests pass |
| Newer drivers/ROCm/ORT | Unknown | Load/negotiate/probe; warn as untested; fallback on actual failure |
| RX 6000 or older, unsupported APUs, arbitrary distro/runtime mixtures | No relevant CK hardware evidence | Community investigation; no blanket promise or version-spoof workaround |

“Build-only” will mean a hosted CI artifact actually compiled/linked. This research did not run those builds. “Community-tested” requires a reproducible report, not merely absence of a bug report. Publish known-broken combinations with the tested workaround and issue link.

## Testing without AMD hardware

1. **Portable contracts now:** fake pipeline, scheduler, crop/resize, model manager, binder, state transitions and diagnostics on CPU. Unit fixtures exercise shape/layout, alpha/foreground semantics and typed provider errors.
2. **Hosted build CI:** compile/link the AMD-specific modules against pinned native SDK artifacts in public CI, with runtime loading optional. Check headers, exported symbols, packaging RPATH/SONAME and missing-runtime behavior. Do not call this a GPU test or require a self-hosted runner.
3. **Graph checks:** inspect static shapes, supported opset, tensor ranks and no unintended CPU partitions where a compiler report is available. CPU numerical parity proves graph semantics, not DML/MIGraphX lowering or performance.
4. **DirectML on available NVIDIA hardware later:** useful for the DML code path, shader math and queue integration. Mark it “DML on NVIDIA tested”; it does not validate AMD drivers or throughput. The design task itself performs no GPU jobs.
5. **Native backend probe:** ship a small deterministic synthetic test, report capability and failure phases, and isolate loader crashes. Include a trivial tensor run, actual static model run, native pixel round-trip, odd dimensions and resize/reset. Test without camera permissions by default.
6. **Community hardware matrix:** request RX 9070-class and RX 7900/7800/7600-class runs on Windows and Linux, one iGPU/dGPU mismatch, EGL/Wayland and GLX where supported. Start with guided edges, then refiner tiles, then two cameras and simultaneous recording/game load. Paid GPU access or private runners require maintainer approval.
7. **Promotion gate:** correct results on known fixtures; no host render wait; no stale-frame overwrite; source-size changes and device-loss recovery; 30-minute soak; measured cold/warm latency, memory, processed FPS and OBS render lag. Use this evidence to promote an interop path to the default. Later compatible drivers still receive runtime probes; the recorded test range is not an exact-version whitelist.

Problems should be reported through **Model manager → Copy diagnostics → Report a problem**, using the fields in [DOCS_PLAN](DOCS_PLAN.md). Include distro/kernel/driver, actual adapter, ORT/MIGraphX/HIP versions, source format/size/FPS, graph/model revision, backend/transport, probe result and relevant log. Reports contain no automatic video upload. Good reports update the public matrix and can broaden a range; version checks are not a substitute for those tests.

## Risks and explicit unknowns

- CK2's exported attention and foundation graphs have not yet been validated on either AMD backend. A provider may compile only fragments, producing unacceptable CPU/GPU transfer cost.
- Native C++ distribution of the current AMD plugin bundle, especially library discovery alongside other OBS plugins, needs verification. Do not require global `LD_LIBRARY_PATH` changes as the normal user solution.
- A Windows HIP runtime is not proof of a Windows MIGraphX EP; no release date for that product capability is promised.
- Shared graphics/compute synchronization and device memory ownership are the riskiest integration work. Readback must remain available even on powerful cards.
- No AMD latency, FPS or VRAM figures in this document are measurements. The community matrix must say what was actually run, and hardware access remains the main evidence gap.
