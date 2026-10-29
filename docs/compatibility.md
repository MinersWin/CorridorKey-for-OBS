<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Compatibility

CorridorKey targets OBS 30.2 and newer on Windows x64 and Linux x86-64.
It builds against the OBS 30.2 API. Unknown newer versions still load and run
capability checks; an unfamiliar version string does not disable the plugin.
A missing runtime or failed provider leaves video in passthrough with an error
and a recovery action. CPU inference is an explicit diagnostic option only.

Evidence below is dated 29 October 2026. A successful load or synthetic backend
test does not establish full CorridorKey inference, image parity or frame rate.

| Label | Meaning |
| --- | --- |
| expected | Targeted by the APIs and fallback policy; no qualifying run yet. |
| build-only | Compilation checked for the named scope; no hardware/runtime result. |
| hardware-tested | Ran on the listed local hardware, within the stated scope. |
| community-tested | A linked report records another user's exact setup and result. |
| known-broken | A specific tested feature failed; the row names its fallback. |

## Hosts

| Host / system | State and exact anchor | Evidence and limits |
| --- | --- | --- |
| OBS 30.2.x | hardware-tested: portable 30.2.0, Windows 11, Qt 6.6.3 | [W07](dev/validation-w07.md), [W15](dev/validation-w15.md): manager, fake pipeline and delay/sync. |
| OBS 31.x | expected | Uses the 30.2 API floor; no 31 host run recorded. |
| OBS 32.x | expected | Uses the 30.2 API floor; no 32 host run recorded. |
| OBS 33.x | hardware-tested: installed 33.0.0, Windows 11, Qt 6.11.1 | [W07](dev/validation-w07.md), [W15](dev/validation-w15.md): manager and fake pipeline. An outstanding shutdown allocation remains unexplained. |
| Compatible newer OBS | expected | [Foundation simulated version test](VALIDATION.md#properties-and-compatibility-checks), [W17 mock policy tests](dev/validation-w17.md). These do not certify a future ABI. |
| Windows 10 x64 | expected | No local Windows 10 run. The host, GPU driver and chosen runtime must each support the OS. |
| Windows 11 x64 | hardware-tested: this machine's RTX 5070 Ti | Host checks above; backend fixture evidence below. |
| Ubuntu 24.04+ x86-64 | expected | [CI build lane](../.github/workflows/build.yml); no local Linux host or distribution runtime test. Match the host's Qt and system-library ABI. |
| Host Qt 6.x | hardware-tested: 6.6.3 and 6.11.1 on Windows | [W07 baseline/current SDK checks](dev/validation-w07.md). The plugin uses OBS's Qt; it does not ship a second copy. |

Windows OBS 30.2â€“32 uses the `bin/64bit` plugin layout. OBS 33+ uses a flat
module beside `data`. Use the package for your host layout; portable OBS has
its own plugin paths. [Foundation layout checks](VALIDATION.md).

## GPU backends

| GPU / backend | State and exact anchor | Evidence, failing feature and fallback |
| --- | --- | --- |
| NVIDIA TensorRT-RTX | hardware-tested: RTX 5070 Ti, Windows 11, driver 616.92, ORT 1.31.1, RTX EP 0.5.0 | [W06](dev/validation-w06.md): synthetic FP32/FP16 Add through host tensors; refiner session compilation. Native video interop and full CK2 are separate checks. |
| NVIDIA conventional TensorRT | hardware-tested: RTX 5070 Ti, ORT 1.31.1, exporter-side TRT 10 | [W06 export evidence](dev/validation-w06.md#real-graph-compilation-and-export-side-evidence): graph timings from the exporter, not the native OBS pipeline. W06 did not execute a conventional TRT runtime. |
| NVIDIA CUDA | hardware-tested: RTX 5070 Ti, Windows 11, CUDA 13 ORT 1.31.1 | [W06](dev/validation-w06.md): FP32/FP16 synthetic fixture. CUDA is a slower graph fallback in the recorded export tests. |
| NVIDIA DirectML | hardware-tested: RTX 5070 Ti, ORT DML 1.24.4, DirectML 1.15.4 | [W11](dev/dml.md): CK2 and refiner D3D11/D3D12 transport, twelve-case live matrix, 30-frame FP32 parity and multiple instances. This is not AMD or Intel evidence. |
| DirectML BF16 fixture | known-broken: W06 Add graph | [W06](dev/validation-w06.md#native-fixture-measurements): session assignment failed. Live preparation excludes BF16; use FP32/FP16 after graph validation. |
| AMD DirectML, Windows | expected; awaiting community-tested evidence | [DML transport](dev/dml.md) checks the same-adapter D3D11/D3D12 bridge and resource bindings. Failed sharing retains staged DML. No AMD hardware result yet. |
| AMD MIGraphX, Linux | build-only: x86-64 ORT 1.31.1 session/probe objects, Zig 0.15.2 Clang | [W17 object compilation](dev/validation-w17.md#linux-compilation-scope). No vendor EP binary was linked or loaded; no AMD hardware run or full Linux plugin link. |
| Intel DirectML, Windows | expected; awaiting community-tested evidence | Same vendor-independent [DML checks and staged fallback](dev/dml.md). No Intel hardware result yet. |

The earlier W06 export snapshot records DirectML stage-graph initialization
failures. W11 ran all twelve FP32 CK2/refiner release graphs with native DML
resource bindings on the RTX 5070 Ti. Use the actual per-graph probe result for
your package; that result does not qualify a different GPU driver.

No community report has qualified a matrix cell yet. A community result needs
the exact host, driver, runtime bundle, graph revision, transport and outcome.

## Runtime selection

Runtime bundles keep exact component hashes for reproducible installation.
Support decisions use negotiated APIs, the matching adapter and actual graph
tests. They do not require every user to have one exact driver, CUDA, TensorRT
or ORT version. Keep matched runtime libraries together; installing a CUDA
development toolkit is not part of normal plugin setup.

Auto tries NVIDIA TensorRT-RTX, conventional TensorRT, CUDA and, on Windows,
DirectML. AMD and Intel use DirectML on Windows; AMD Linux uses MIGraphX.
Missing optional features use an older API or a staged transport when available.
An unavailable provider affects that backend; it does not prevent the model
manager from opening. [Bundle selection and ABI policy](dev/runtime-bundles.md).

An incompatible required ABI or missing required library symbol can prevent
safe loading. OBS's loader may reject an unsatisfied import before the plugin
can log its own explanation. The plugin's host-policy checks name the missing
symbol when they can run. [Policy tests and limits](dev/validation-w17.md).

## Report a problem

Open **Tools > CorridorKey Models > Copy diagnostics**. Review the preview,
then choose **Copy to clipboard**. **Report a problem** opens the
[GitHub issue chooser](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose)
without including your report in the URL. Paste the reviewed report yourself.
The plugin never uploads it.

The report includes versions/build IDs, GPU drivers and adapter identities,
runtime bundle and graph hashes, backend availability, safe settings, recent
timing and error codes. Missing measurements remain unavailable. GPU LUID/PCI
identities describe adapters; scene/source names and source UUIDs are omitted.
Home/profile paths become placeholders; arbitrary errors, tokens, media and
free-text settings are excluded. [Collector details](dev/diagnostics.md).

An OBS log is optional. Open **Help > Log Files > View Current/Last Log**, review
it and attach only what you want to share. OBS logs can include device names,
source names and personal paths outside the plugin's report.
