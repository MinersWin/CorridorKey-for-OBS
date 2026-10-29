<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W06 validation, 29 October 2026

## Environment and scope

Windows 11 x64, Ryzen 9 5950X, RTX 5070 Ti 16 GB (`sm_120`). `nvidia-smi`
reported driver 616.92 and CUDA driver API 13.4. CUDA and DXGI identified the same
adapter LUID. No OBS rendering/interop test was run, so this establishes adapter
matching between compute and DXGI, not a live OBS transport.

The build used MSVC 19.44.35229, the Windows 10.0.26100 SDK and the shared VS 2022
Build Tools. The ORT-enabled plugin build used the OBS 30.2 baseline SDK and its
Qt package. Downloads, generated fixtures, runtime DLLs, profiles and caches stayed
under the W06 worktree's ignored `.cache`; no model/runtime binaries are committed.
The installed OBS was not started or modified.

GPU load was checked before testing. The final tiny-graph series started at 8%
utilization and 3353 MiB used; other desktop/research processes remained active.
Tests ran sequentially, with one first run and five timed runs per configuration,
under a 25-second native helper deadline. These are smoke measurements with
profiling enabled and host-bound tensors. They do not predict CorridorKey FPS,
native GPU transport latency, or steady-state performance under streaming load.

## Build and failure-containment checks

- ORT-disabled portable build: passed compilation and CTest.
- ORT-enabled Debug and Release builds: passed compilation and CTest.
- OBS 30.2 SDK plugin build with ORT headers: passed. Import-table inspection
  found no ORT or NVIDIA runtime dependency in the plugin or native helper.
- Local install staging placed the plugin, native helper and nlohmann/json
  licence under the expected plugin tree. Optional runtime DLLs were not copied.
- SHA-256 known vectors, manifest traversal/hash rejection, adapter mismatch
  and PCI normalization, Auto ordering and explicit-only CPU diagnostics: passed.
- Cache identity changes, simultaneous private working copies, interrupted
  publication and corrupt metadata: passed.
- Helper crash, timeout, cancellation before/during execution, malformed result
  and paths with spaces: passed. Windows job objects terminated/reaped helpers.
- Synthetic ORT library: requested API 31 down to 19, accepted a `999.0.0`
  version string, reused the same private runtime, and rejected a second core
  at a different path. This tests policy, not compatibility with a real future ORT.
- Optional real CPU integration test: `DisposableProbe`, helper compilation,
  `prepare_session`, exact IOBinding output, cancellation and subsequent reuse
  passed. The resulting capability did not permit live CPU inference.
- Per-graph preparation tests retained candidate precision and excluded BF16
  and CPU from live graph fallback.
- Source formatting uses the repository's clang-format 19.1.7 and gersemi 0.22.2.

The ordinary tests use a mock runtime and never start a GPU job. The optional real
CPU test runs separately to avoid deliberately colliding its core with the mock:
set `CK_TEST_BUNDLE`, `CK_TEST_GRAPH`, and `CK_TEST_HELPER`, then run
`corridorkey_runtime_tests --test-case=real*`.

## Native fixture measurements

The fixture comes from W01's `generate-test-graph.py`: opset 18, IR 8, static
`[1,1,2,2]`, one Add with independently authored constants. W06 generates typed
FP16/BF16 copies. Every successful row produced exact expected output and six
profiling events for the requested GPU provider, with no CPU partition. Warm
numbers are medians of five synchronized IOBinding calls, including host/device
copies and ORT profiling overhead.

Preparation includes file-integrity checks, runtime/EP loading, adapter discovery
and session construction. It is not an isolated TensorRT engine-build timer.
The RTX rows used initially empty plugin cache entries. Driver-level caches were
not deleted. CUDA's first ever diagnostic run earlier in the session took
7707.5 ms; the final series benefited from existing driver state.

| Provider / input dtype | Core / EP | Preparation ms | First run ms | Warm median ms | Result |
| --- | --- | ---: | ---: | ---: | --- |
| RTX FP32 | ORT 1.31.1 / RTX EP 0.5.0 | 955.63 | 0.676 | 0.107 | Exact output, RTX partition |
| RTX FP16 | Same | 919.47 | 0.411 | 0.109 | Exact output, RTX partition |
| RTX BF16 | Same | 892.03 | 0.555 | 0.203 | Diagnostic fixture only |
| CUDA FP32 | ORT GPU CUDA 13 1.31.1, classic append | 1173.41 | 146.694 | 0.126 | Exact output, CUDA partition |
| CUDA FP16 | Same | 1079.83 | 149.223 | 0.249 | Exact output, CUDA partition |
| CUDA BF16 | Same | 1086.93 | 162.160 | 0.099 | Diagnostic fixture only |
| DirectML FP32 | ORT DML 1.24.4 / DirectML 1.15.4 | 411.13 | 2.625 | 0.207 | Exact output, DML partition |
| DirectML FP16 | Same | 390.93 | 2.618 | 0.223 | Exact output, DML partition |
| DirectML BF16 | Same | Failed at session creation | — | — | Add could not be assigned to DML; CPU fallback remained disabled |

RTX wrote approximately 8.8 KB of runtime-cache data per precision. A fresh
process using the populated FP32 cache prepared in 798.43 ms, ran first in
0.406 ms, and had a 0.128 ms warm median. The short, contended samples do not
establish a throughput improvement. The cache implementation snapshots a private
working directory and publishes only after session teardown, because RTX 0.5.0
writes its runtime cache when releasing the execution context.

ORT 1.31.1 negotiated API 31; DirectML's older core negotiated API 24. NVIDIA's
RTX 0.5 test matrix lists ORT through 1.30.0. The 1.31.1 combination was attempted
and passed this fixture; it is not rejected merely for being outside that matrix.
[NVIDIA test matrix](https://github.com/NVIDIA/TensorRT-RTX-EP-ABI/tree/v0.5.0#test-matrix).

The GitHub CUDA DLL initially failed direct plugin loading with Windows error
1114. Inspection showed exports for both `GetProvider` and `CreateEpFactories`.
Using classic `SessionOptionsAppendExecutionProvider_CUDA_V2` succeeded. The
tested descriptor records `classic`; a standalone CUDA plugin wheel needs its
own descriptor and validation.

## Real graph compilation and export-side evidence

The coordinator supplied read-only `refiner_b384.onnx` and its external weights.
The native helper created static FP32 sessions on all three requested providers:
RTX 1640.82 ms, CUDA 1322.91 ms, DirectML 907.01 ms. RTX first failed under an
explicit 64 MiB workspace cap and succeeded with its default workspace setting.
These were compilation-only tests: no refiner inputs were submitted and no
parity or inference latency is claimed for W06's C++ implementation.

The coordinator also supplied `onnx_test/parity.jsonl`. Its records were read and
checked, not rerun. The following values are export-side device-only medians on
the same GPU with ORT 1.31.1, using conventional TensorRT, not TensorRT-RTX:

| Graph | Export-side precision | TRT device ms | CUDA device ms |
| --- | --- | ---: | ---: |
| `stage1nomask_256x512` | FP16 | 7.329 | 54.395 |
| `stage2_256x512` | FP16 | 5.594 | 38.749 |
| `refiner_b384` | FP16 | 2.035 | 17.022 |
| `rvm_256x512` | FP32 | 3.214 | 12.453 |
| `moge_256x512` | FP32 | 2.677 | 6.862 |

RVM/MoGe remain FP32 under the coordinator's policy. The FP16 parity records show
MoGe feature max absolute error 0.4450 and RVM state max absolute error 0.0924;
passing a tiny typed Add is insufficient evidence to change that policy. BF16
is excluded from live preparation despite the successful diagnostic rows above.
CUDA remains a fallback; its real-graph timings are substantially slower than TRT.

The export-side DML records report successful refiner/RVM/MoGe runs, but
`stage1nomask`, `stage1`, and `stage2` at 256x512 fail initialization with
`E_INVALIDARG` (`0x80070057`). That ongoing investigation was not repeated here.
`prepare_graph` therefore takes ordered backend/precision alternatives per graph,
compiles the actual graph in the disposable helper, and preserves failure details.
If a required graph has no acceptable backend, W09 must report its failure and
keep passthrough; a successful refiner probe cannot authorize the stage graphs.

Import inspection independently confirmed that this ORT archive's conventional
TRT provider needs `nvinfer_10.dll`, `nvonnxparser_10.dll`, `cudnn64_9.dll` and
`cublas64_13.dll`. A TensorRT 11.x wheel does not satisfy those imported DLL names.
No conventional TensorRT runtime binary was installed or executed by W06.

The session API accepts prebuilt EPContext graphs and separate session/provider
options. Setting `prebuilt_context` avoids injecting the conventional FP16 build
flag. A separate strongly typed TensorRT builder remains future helper work;
its numerical advantage has not been independently verified here. The desktop
project's documentation was consulted for the idea only; no implementation was
copied. ORT's own documentation describes the precompiled-engine route.
[ORT EPContext engine loading](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html#embed-engine-model).

## Remaining verification and release decisions

- Linux process handling, `dlopen`/RUNPATH, OpenSSL linkage, MIGraphX and AMD
  hardware have no W06 build or hardware result. WSL was unavailable. Do not label
  these paths community-tested or build-tested yet.
- No OBS runtime-load/interop test, multigpu machine, device loss, stream soak or
  full CK2 parity/performance run was performed. Native device IOBinding memory
  requires the W10/W11/W12 transport tests; W06 reports only tested host tensors.
- Conventional TRT engine/timing caches and the standalone CUDA plugin artifact
  remain untested. The confirmed TRT 10 imports constrain the next bundle, rather
  than imposing a blanket version restriction on all future bundles.
- W19 must choose/build a core supporting the intended NVIDIA and DML providers.
  Current test bundles use different cores; seamless mixed DML/NVIDIA sessions in
  one OBS process are not validated and the loader will refuse that collision.
- W09 owns graph-specific precision/parity policy, conversions between stage
  tensor types, CPU-partition exceptions if any, and bounded recovery. The current
  session policy rejects all CPU fallback for GPU graphs.
- Vendor binary redistribution needs the component notices described in
  [runtime bundles](runtime-bundles.md). In particular, the RTX wheel lacks
  standalone licence files; its native test assembly is not a release package.

The implementation changed no shared C++ contracts or root CMake structure.
W06 owns its CMake fragment; the existing ORT descriptor now exposes headers only.
The shared third-party notice file gained entries for nlohmann/json and OpenSSL.
