<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Windows D3D11/CUDA transport

The NVIDIA CK2 worker keeps model tensors and refiner tiles on the device. It
uses the production model cache and reader leases. The development configuration
is only read when `CORRIDORKEY_CK2_DEV_CONFIG` is set in a development build.

```mermaid
flowchart LR
    source[OBS source] --> native[Retained native D3D11 frame]
    native --> grid[Grid shader: RGBA8]
    grid --> shared[GPU copy to shared grid texture]
    shared --> input[Worker D3D11 device: CUDA registration and planar conversion]
    input --> models[Persistent ORT CUDA tensors]
    models --> low[CUDA pack into D3D11 low-resolution output]
    low --> post[Guided reconstruction shaders]
    native --> post
    post --> atlas[Tile gather shader: RGBA32F atlas]
    atlas --> sharedAtlas[GPU copy to shared atlas]
    sharedAtlas --> unpack[CUDA atlas unpack]
    unpack --> refiner[Captured TensorRT refiner]
    refiner --> centres[CUDA centre pack into D3D11 texture]
    centres --> blend[Tile blend and final composite shaders]
    post --> blend
```

There are device-to-device copies between mapped CUDA arrays and linear buffers.
The frame and tile atlas never pass through host memory on this path. The
retained native texture stays in D3D11 because reconstruction and gather consume
it directly; registering it would add ownership transitions without a CUDA
consumer. A 32x32 RGB thumbnail and periodic ROI alpha statistics still use host
control code. These are separate from the full-frame and atlas transfers.

## Ownership and synchronization

`D3D11Texture` uses the dynamically loaded driver equivalent of
`cudaGraphicsD3D11RegisterResource`. It opens shared textures on a separate
worker-owned D3D11 device on the same adapter. Direct registration on OBS's
device deadlocked during live testing: captured thread stacks showed CUDA
registration waiting on a D3D11 device lock while OBS's render thread waited
inside the NVIDIA driver. The separate device avoids that lock inversion.

Registration checks both the DXGI adapter LUID and CUDA ordinal. DEFAULT-usage,
single-mip, single-sample shared RGBA8, BGRA8 and RGBA32F textures are supported.
The capture grid uses typed `GS_RGBA_UNORM`; OBS's `GS_RGBA` resource is typeless
and cannot be registered by this implementation. Other formats take the staged
path. The renderer copies its grid and gathered atlas into shared textures.
CUDA writes low-resolution outputs and refined centres through shared aliases.

OBS draws a slot and signals its D3D11 completion fence. When `ID3D11Device5`
and `ID3D11DeviceContext4` are available, it flushes the submitted commands and
hands the slot to a worker immediately. The worker waits on
`ID3D11Fence::SetEventOnCompletion`, with a one-second failure timeout. Older
runtimes use a completion query polled on later graphics ticks. The interfaces
are detected at runtime; they are not a minimum plugin-loading requirement.

After completion, the worker maps the registered alias, copies its CUDA array,
and unmaps it. It completes the CUDA stream before publishing the shared output
to OBS. The slot lease prevents reuse during this work. The atlas uses the same
event handoff. A video tick can finish one refinement and start the next gather;
both publication attempts are nonblocking. Polling each phase on a separate tick
previously limited the live shared-texture path to about 20 refined keys/s.
These fences and exclusive leases provide the ownership protocol, so a keyed
mutex would duplicate it. Registration, CUDA allocation, mapping, synchronization
and unregistering run on workers. Render
callbacks only submit graphics work and poll existing completion objects.

CUDA's [graphics interoperability contract](https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__GRAPHICS.html)
orders graphics work before the mapped CUDA stream, and CUDA work before later
graphics use after unmap. The explicit event and stream completions above also
cover the second D3D11 device used by OBS.

The input worker owns a bounded registration cache which retains the underlying
D3D11 textures. It clears registrations on worker shutdown. Published frame
objects contain texture views, so graphics-thread retirement does not free CUDA
allocations. The refiner worker owns its atlas and centre registrations and
persistent conversion buffers.

Capture conversion and low-resolution packing reuse one buffer per registered
slot. The CK2 sequence also retains its thumbnail, RVM and MoGe resize buffers.
These allocations are created and released on workers. A slot remains leased
until its model output and graphics reads finish, so the next capture cannot
overwrite a tensor still in use.

ORT keeps its own provider streams. Bindings synchronize inputs and outputs;
conversion streams are completed before inference. Supplying a common external
stream to the local ORT 1.31.1 / TensorRT 10.15 / CUDA 13 bundle caused a native
cuDNN handle failure during qualification, so this implementation does not use
`user_compute_stream`. CUDA event intervals bracket synchronized ORT calls and
include submission gaps. They are not kernel-only profiler measurements.

## Tensors, precision and capture

Graph inputs and outputs use fixed CUDA allocations wrapped by ORT values and
IOBinding. The exported interfaces remain FP32; TensorRT uses FP16 internally
for Stage 1, Stage 2 and the refiner, and FP32 for RVM and MoGe. RVM copies its
state output into fixed state input buffers before the next step. Captured
addresses therefore remain unchanged. MoGe features remain resident between
refreshes.

RVM, Stage 1, Stage 2 and the refiner request `trt_cuda_graph_enable=1`. MoGe
remains uncaptured. Changing ROI values does not recapture a fixed-shape graph.
Session recreation is still required for a grid, backend or transport change.

Each captured session is warmed twice with its fixed bindings before the
pipeline accepts live frames. A capture made by the helper belongs to that
helper process and does not warm the session inside OBS.

The small conversion kernels are authored in `src/gpu/cuda_kernels.cu`.
`tools/compile-cuda-kernels.py` uses a local NVRTC installation to regenerate the
checked-in PTX. Runtime compilation and a CUDA toolkit are not needed on the
user's machine. The plugin loads driver entry points at runtime and has no CUDA
import-library dependency. The refiner unpack rounds through FP16 to reproduce
the existing shader/CPU contract. Gather, selection and blending remain OBS
shaders.

The checked-in PTX targets compute capability 7.5. The driver must accept its PTX
version as well as support the device. Kernel loading is tested during model
preparation; a failure selects staged tensors. A 16-bit source is converted by
the existing grid shader before registration. Direct RGBA16F registration is
not implemented; the shared atlas and output use RGBA32F to preserve the W13
conversion contract.

## Cache and fallback

W06 cache identities include graph and weights hashes, shape, precision, provider
options, adapter, driver and runtime/provider build identity. The helper process
prepares TensorRT engines where supported. A failed TensorRT preparation retries
once with a fresh private cache; atomic publication and existing reader leases
remain in force. No repeated rebuild loop is introduced.

Windows cache publication uses extended paths internally, and the private
directory name omits the redundant identity hash. With the old names, the
portable OBS cache exceeded traditional path limits: timing files survived but
engine publication failed. The corrected helper published the 927,582,156-byte
512x512 Stage 1 engine and warmed capture successfully. A regression test covers
long portable paths and reuse of a serialized engine filename.

Session creation also normalizes graph paths to native Windows separators.
TensorRT's external-weight resolver dropped the `graphs` directory when the
production catalogue supplied a mixed-separator path. Repeated failed graph
partitions then reached about 69 GB of committed host memory. With normalized
paths, production Stage 1 activates with capture enabled; the helper and OBS
resolve the same bound weights.

Automatic selection attempts device transport only with resident NVIDIA tensors
and successful texture registration. Missing CUDA APIs, a different adapter,
unsupported texture usage/format or failed conversion disable the affected
interop path. The next capture uses staged readback. A failed atlas path uses
the staged refiner. DirectML and other existing providers retain their staged
transport. The status line and W17 diagnostics report the selected input
transport; W13 logs separately report atlas readback/upload byte counts.

## Reproducing validation

Use `tools/prepare-interop-validation.py --help` to generate an ignored local
configuration from revision-3 exports. It verifies graph and manifest hashes,
copies those small files, and links or copies bound weights and NPZ references.
It never modifies the export directory. Keep the output directory outside
version control. The helper, runtime descriptor, adapter LUID and standard-op
probe fixtures are explicit arguments.

Build with `CORRIDORKEY_CK2_DEV=ON`. Run `corridorkey_interop_test` manually on an
NVIDIA D3D11 device. Use the [Q1 harness](quality-parity.md) to regenerate research
frames, then the [W13 harness](refiner.md) with `--interop` on both render steps.
`quality-parity.py inference --backend tensorrt` runs the resident CK2 sequence;
the legacy `dml-*` filenames are retained for compatibility with the comparison
commands. Numeric fixture dumps intentionally use host memory in the harness.
They are not part of the live transport.

For live production tests, copy a prepared model cache into a dedicated portable
OBS instance. Do not set the development override. Install the matching plugin,
catalogue, probe fixtures and runtime descriptors into that copy. Record whether
other GPU workloads are running, the profile, tile budget, source size, transport,
new keys/s, W15 displayed age and whole-device memory.

### One-pass performance matrix

`tools/interop-matrix.json` defines all 24 cases: Facecam/384, Balanced/512 and
Quality/768; refinement on/off; shared textures/staged transfers; 1080p/4K input.
`tools/measure-interop.py` applies each case, waits for keyed output, settles for
15 seconds and takes 35 GPU samples with one-second pauses. The saved timestamps
include query overhead; these measurement windows lasted about 38 seconds.
It saves raw telemetry, CUDA stage intervals,
displayed-age summaries, whole-device VRAM and GPU process IDs. An existing
result file is retained, so use a new output directory for a clean rerun.

`tools/summarize-interop.py RESULTS --cases tools/interop-matrix.json --output
SUMMARY.json` checks completeness, tile budgets, atlas transfers and Stage 2
activation. It retains both W09 published keys/s and W15 displayed keys/s.
Publication can be faster than display when a result is replaced before OBS
draws it. Displayed age and rate are medians of the logged five-second windows;
stage intervals and publication rate are means of the logged ten-second windows.
MoGe timing is per refresh, not per video frame.

Prepare a dedicated portable OBS with a 60 FPS canvas, an image source named
`W09 Facecam` and its `CorridorKey` filter. Supply the same image resized to
1920x1080 and 3840x2160. The script changes the source dimensions and records the
canvas settings; it does not change the canvas. Enable only this test copy's
local websocket on port 18110 without authentication, and disable it afterward.
The measurement uses a 1000 ms stale-frame limit so slow staged results remain
observable. The normal 250 ms limit can discard those results instead.

Install all three production profiles before starting. If only the W09b cache
is available, it contains Balanced and the refiner, so Facecam requires the
generated development config. Launch the development plugin with
`CORRIDORKEY_CK2_DEV_CONFIG` pointing to that config for the complete matrix.
Record that choice in `--notes`. Production activation is qualified separately
without this environment variable. Prepare the graphs once before reserving
the GPU; first-time engine builds can exceed the sampling time.

During the agreed GPU window, check `nvidia-smi` and the other workers before
creating `S:\Projects\obs-test\gpu-window.lock` containing `w10-interop`.
Do not overwrite another worker's lock. Run from the W10 worktree:

```powershell
uv run --no-project --with websocket-client python tools/measure-interop.py `
  --obs S:/Projects/obs-test/obs-33.0.0-w10 `
  --cases tools/interop-matrix.json --output build-validation/idle-matrix `
  --image-1080 build-validation/dance1080.png `
  --image-4k build-validation/dance4k.png `
  --gpu-condition reserved-window --notes "Revision-3 development config; production activation checked separately"
```

Inspect every case for the expected transport and nonzero new keys/s. Refined
interop samples must show zero atlas readback/upload bytes; staged samples must
show the atlas transfer. Record any competing compute process as shared GPU,
even during a reservation. Once measurements and any required reruns finish,
remove the lock and create `gpu-window.done` so the export worker can resume.
Stop the test OBS and restore its websocket and stale-frame settings afterward.

## Numerical qualification

The [retained numeric summary](interop-parity.json) records 30 frames from ten
segments at the Facecam grid and 384 tiles, using revision-3 graphs, TensorRT
FP16 stages/refiner and FP32 foundations on an RTX 5070 Ti.

| Check | Measured | Limit |
| --- | ---: | ---: |
| Atlas channel maximum error | 0.00048828125 | 0.0005 |
| Worst frame/channel MAE | 0.00000242933 | 0.000005 |
| Tile-score maximum error | 0.000000178814 | 0.000001 |
| Selected tile set differences | 0 | 0 |
| Same-input reconstruction regional MAE | 0.00171079 | 0.002 |
| Full-pipeline teacher-error gap | 0.00441819 | 0.005 |
| Q1 reconstruction MAE, refinement off | 0.00148942 | 0.002 |
| Q1 teacher-error gap, refinement off | 0.00155226 | 0.005 |

All Q1 and W13 gates pass. Synthetic RGBA/BGRA round trips cover 1x1, odd dimensions,
the Facecam grid, 1080p and 4K. RGB conversion has zero measured error and alpha
is preserved. Atlas unpack matches the staged implementation exactly at budgets
256, 384, 512 and 768. Tests also cover nonfinite tensor rejection, unsupported
dynamic D3D11 textures, independent allocations on two CUDA contexts and 300
concurrent shared-texture round trips. The post probe's `fence` mode checks 64
signal/wait/reuse cycles with the wait on a separate thread. These
checks do not certify temporal quality over full clips, Balanced/Quality parity
or multiple physical adapters.

After retaining conversion and resize buffers, the three-frame curly-blond
sequence produced bit-identical alpha and foreground to the qualified run.
The hardware test also clears and refills reused buffers before comparing them
with the CPU oracle.

A WARP texture is rejected before registration because its rendering adapter
has no matching CUDA device. Live OBS testing also ran two production Balanced
filters concurrently, one with 512-tile refinement and one without. Both used
shared textures; the second completed over 1000 frames and was removed while
the first continued rendering. Source changes between 1080p and 4K, refiner
toggles and explicit staged selection completed without restarting OBS.

The Windows build uses the OBS 30.2 SDK and was exercised in portable OBS 33.
All 15 CTest groups pass, including native binder parity against 14 CK2 export
files. The local-model check uses Python 3.12 and `CK_BINDER_EXPORTS` pointing
at the regenerated CK2-only fixture directory. Sixteen changed runtime source
files also compile to Linux x86_64 objects with Zig 0.13/Clang, C++20 and
`-Wall -Wextra -Werror`. This checks Linux source branches, not Linux GPU
execution or GCC CI. Clang-format 19 and gersemi 0.22.2 pass.

## Transfer microbenchmark

On the reserved RTX 5070 Ti, with OBS and other compute jobs stopped,
`corridorkey_interop_test --benchmark` ran 100 warm iterations per size. Each
iteration reads a shared RGBA8 texture, converts to planar FP32 RGB, packs RGBA32F
and writes a shared output texture. Registration, allocations and CPU oracle
readback are outside the timed loop.

| Texture size | Host elapsed per round trip | CUDA event interval |
| --- | ---: | ---: |
| 512x256 | 0.221 ms | 0.221 ms |
| 1920x1080 | 0.412 ms | 0.412 ms |
| 3840x2160 | 1.179 ms | 1.179 ms |

The 4K result is in the same range as the Python bridge prototype's roughly
1.3 ms round trip. This loop includes format conversion and device copies, but excludes
OBS capture, model inference and reconstruction. It cannot establish new keys/s
or displayed age by itself.

## Live matrix

The [numeric results](interop-performance.json) retain publication rate, displayed
rate, displayed-age percentiles, CUDA stage intervals, VRAM and render skips.
The table uses distinct keys actually displayed per second. VRAM is peak
whole-device usage in MiB; each paired cell is staged / interop.

| Profile | Source | Refiner | Staged keys/s | Interop keys/s | Age ms, staged / interop | VRAM MiB, staged / interop |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Facecam | 1080p | Off | 54.9 | 58.0 | 33.5 / 16.8 | 3810 / 3764 |
| Facecam | 1080p | On | 11.6 | 56.0 | 161.2 / 33.5 | 3988 / 4200 |
| Facecam | 4K | Off | 55.1 | 58.0 | 33.5 / 16.8 | 4590 / 4484 |
| Facecam | 4K | On | 11.6 | 56.0 | 160.8 / 33.5 | 5075 / 5146 |
| Balanced | 1080p | Off | 50.7 | 57.8 | 50.1 / 16.8 | 4644 / 4580 |
| Balanced | 1080p | On | 9.7 | 55.5 | 200.5 / 33.5 | 4954 / 4480 |
| Balanced | 4K | Off | 50.3 | 51.0 | 50.2 / 16.8 | 5465 / 5702 |
| Balanced | 4K | On | 10.5 | 42.9 | 186.4 / 33.7 | 6096 / 5469 |
| Quality | 1080p | Off | 31.5 | 52.5 | 67.3 / 33.5 | 5361 / 5299 |
| Quality | 1080p | On | 6.4 | 42.9 | 300.8 / 50.6 | 5814 / 6067 |
| Quality | 4K | Off | 30.0 | 48.3 | 75.2 / 33.6 | 6183 / 6040 |
| Quality | 4K | On | 7.2 | 40.4 | 286.0 / 66.8 | 6932 / 7166 |

The 4K Facecam target of 60 distinct refined keys/s was not reached. It
published 58.0 keys/s and displayed 56.0, with 33.5 ms median age. Without
refinement, its displayed age was 16.8 ms. Retaining conversion buffers raised
Facecam publication from 56.6 to 58.0 keys/s in the reserved window; median age
remained 33.5 ms.

OBS reported zero render skips in every interop measurement window. Staged
Quality with refinement skipped six render frames at 4K and one at 1080p;
the remaining staged windows had none. Its median ages of 286 and 301 ms exceed
the normal 250 ms stale-frame limit.

CUDA event means for the 4K interop cases with refinement enabled:

| Profile | MoGe per refresh | RVM | Stage 1 | Stage 2 | Refiner median | CK2 worker mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Facecam | 3.03 ms | 1.83 ms | 3.28 ms | 0.00 ms | 1.82 ms | 8.41 ms |
| Balanced | 3.14 ms | 1.64 ms | 3.86 ms | 0.00 ms | 2.40 ms | 11.15 ms |
| Quality | 2.67 ms | 1.13 ms | 4.73 ms | 7.50 ms | 3.95 ms | 22.08 ms |

The research engine reported about 11.1 ms for Facecam and 15.6 ms for Balanced
without transfer. The CK2 worker column excludes refinement and graphics
publication, while the CUDA columns include synchronized submission gaps.
Those boundaries and the static input differ from the research run, so the
numbers do not establish an inference speedup over that engine.

The measured plugin predates a logging-only correction: after switching from
staged to interop, `read_ms` could retain its previous value. Atlas readback and
upload byte counts correctly remain zero on the interop path.

## Measurement conditions and remaining limits

The live matrix uses portable OBS 33.0.0 on Windows 11, an RTX 5070 Ti with
16,303 MiB reported memory, driver 616.92, a Ryzen 9 5950X and 64 GB system RAM.
The runtime bundle is ORT 1.31.1, TensorRT 10.15 and CUDA 13. Revision-3 graphs
come from the regenerated development configuration. Production activation was
tested separately with the copied W09b cache, including Balanced and Quality
with their refiners.

Each case uses the same static dance image at either 1920x1080 or 3840x2160 on
a 3840x2160, 60 FPS canvas. There is no encoder or browser source. The GPU was
reserved for this test; desktop applications remained open. Whole-device VRAM
includes OBS, desktop apps and allocations retained from earlier cases in the
same process. The initial desktop baseline was 1668 MiB. Separate launches would
be needed to establish per-profile memory requirements. Measurements use a
1000 ms stale-frame limit; the shipped 250 ms limit can reject slow staged
results. These conditions should be retained when comparing another run.

The staged control still uses W10's persistent TensorRT bindings and CUDA graph
capture. It transfers model results and the refiner atlas through host memory.
It is therefore a transport comparison, not a reproduction of the uncaptured
pre-W10 build. Earlier shared-GPU runs are kept separately and are not used for
the reserved-window comparison.

Warm activation still takes minutes because preparation verifies the runtime,
opens the graph in the helper, then creates and warms the in-process session.
For the Facecam staged run, Stage 1 took 60.9 seconds, RVM 44.8 seconds and MoGe
15.9 seconds before refiner preparation. Cache reuse does not remove those costs.

CUDA/graphics ownership transitions are serialized across workers. Conversion
and model streams synchronize at their handoff points, and RVM state is copied
between fixed buffers. Further overlap needs separate synchronization and parity
qualification. Multiple physical adapters, other NVIDIA generations, AMD/Intel
hardware and Linux GPU execution remain unmeasured. The static live matrix also
does not establish streaming performance with a moving source and an encoder.
