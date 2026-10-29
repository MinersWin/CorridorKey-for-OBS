<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CK1 validation, 29 October 2026

Tests used Windows 11, an RTX 5070 Ti 16 GB, driver 616.92 and portable OBS
33.0.0. The plugin was built against the OBS 30.2 baseline SDK with warnings as
errors. The branch includes W10 at `b33fe0f`. Artifact provenance is recorded in
[validation-w14-artifacts.json](validation-w14-artifacts.json); numerical results,
run samples and test conditions are in [validation-w14.json](validation-w14.json).

## Packages and bindings

All three production packages passed missing-licence rejection, explicit local
acceptance, native binding and reader-lease acquisition in a fresh catalogue
cache. Each contains 256, 384 and 512 square variants. All nine bound weight
files matched the exporter's files byte for byte. The checkpoint sizes and
SHA-256 values matched anonymous HF listings at the full pinned revisions.
The 18 graph/manifest downloads matched the published listings and revision-4
hash fixture. No exporter files were modified.

## Output comparisons

Every standalone output and all six MODNet-to-CK1 chains were compared with the
exporter's FP32 `.ref.npz` files. The reference RGB and MODNet matte exactly
match the CK1 reference inputs at each grid, so these chained comparisons use
the same inputs. Both screen checkpoints were exercised. NVIDIA outputs stayed
resident until the final comparison download. Provider profiles contained only
the selected GPU provider.

| Execution | Largest absolute difference | Largest mean absolute difference |
| --- | ---: | ---: |
| DirectML FP32, standalone | 0.00326318 | 0.0000166330 |
| DirectML FP32, MODNet chain | 0.00326067 | 0.0000166138 |
| TensorRT FP16, standalone | 0.0141570 | 0.000152143 |
| TensorRT FP16, MODNet chain | 0.0116361 | 0.000153646 |

The maxima and means are separate maxima over all compared tensors. The largest
FP16 standalone difference is in green-384 foreground. These are measured
differences, not a newly chosen release tolerance. The tool rejects wrong
shapes and non-finite values; this table preserves the numerical differences
for review. Export-side edge-quality results remain the figures in the
[user guide](../user/ck1.md), measured on the release evaluation's real clips.

The CK2 Stage-1 hint was also run through its reused MoGe/RVM/Stage-1 path at
512x512 with CK1 green-384. Its output is finite and in range. This hint differs
from the MODNet reference, so it has no matching chained parity claim.

## Tensor-path timings

Values below are median / p95 milliseconds for the complete MODNet-to-CK1
sequence. Cold session preparation is excluded. ORT profiling was enabled.

| Grid | TRT green | TRT blue | DirectML green | DirectML blue |
| --- | ---: | ---: | ---: | ---: |
| 256 | 2.82 / 2.92 | 2.83 / 2.93 | 12.57 / 13.50 | 12.62 / 13.14 |
| 384 | 4.58 / 4.68 | 4.50 / 4.59 | 23.92 / 29.02 | 22.76 / 25.47 |
| 512 | 6.93 / 7.13 | 7.35 / 7.60 | 41.26 / 48.23 | 40.60 / 48.61 |

The final NVIDIA runs took place after the training process exited. No competing
live GPU inference or OBS process was reported; desktop applications stayed open.
WDDM process entries were checked for process liveness. Utilization snapshots can
include the preceding W14 run. Each run recorded 101
iterations and excluded the first 21, leaving 80 samples. CK1 and MODNet used
FP16 TensorRT with CUDA graph capture and W10's persistent resident tensors.
Fixture upload and final comparison download are outside the measured interval;
device binding copies, validation and host submission are included. These runs
do not include D3D11 frame capture, reconstruction or OBS presentation.

DirectML used ORT 1.24.4 / DirectML 1.15.4 with FP32 graphs and staged tensors.
Those runs preceded the W10 rebase and reported no competing inference or OBS
process. They recorded 21 iterations, excluding the first. Their host bindings
and tensor transfers are included. The post-rebase portable OBS check below
exercises that staged fallback with the current plugin.

The DirectML CK2-hint test measured 59.32 ms median for the complete hint and
79.70 ms for the complete CK1 sequence, p95 90.98 ms. This includes resizing and
the CK2 foundation models. It is an expensive hint, even with CK1's refiner off.
Earlier shared-GPU NVIDIA results are retained in the JSON with their conditions
and are excluded from the table above.

## Portable OBS

The portable test copy used the normal production catalogue, accepted model
receipts and native bindings. No development model override was set. Media
decode ran on the CPU; the source was 1920x1080 at an OBS rate of 60 fps. The
test collection contained no browser or global audio sources. Only this private
OBS copy was started or stopped. No firewall rules or installed SDK files changed.
Process-local NVIDIA effects SDK paths pointed to an empty test directory to
avoid the known TensorRT DLL collision.

DirectML rendered MODNet keys at all three grids, Alpha and raw Hint views at
384, and the Expert chroma path. A stable green-384 window logged 35.92 new
keys/s, 66.90 ms median displayed age, 83.59 ms p95 age and 26.25 ms median
inference. The properties reported a measured MODNet hint cost included in
inference. These live figures include the staged frame path and OBS scheduling.

NVIDIA selected `transport=d3d11-cuda`. Both CK1-384 and MODNet-384 logged
`capture=on`. A stable motion window logged 60.00 new keys/s, 16.82 ms median
displayed age, 16.99 ms p95 age and 4.54 ms median inference. Median input and
output phase times were 1.16 and 9.97 ms, including their scheduling boundaries.
The measured last-frame MODNet hint cost was 1.04 ms. OBS reported no skipped
render frames during the captured run. The same source, grid and reconstruction
settings were used for DirectML and NVIDIA.

First production-cache preparation took 137.44 seconds for CK1 and 93.54 seconds
for MODNet. The first key appeared about 237 seconds after the settings update.
These cold costs are separate from steady frame timings. The portable host was
restarted between DirectML and NVIDIA because their ORT cores differ.

The [source renders](../images/ck1/provenance.md) were captured through OBS's
source-screenshot API, without a cursor. The filtered views share a paused
reporter frame, including the microphone prop and gesturing hand. The unfiltered
context image was captured separately. The PNG keys preserve
transparency. No UI or keyed output was fabricated.

## Checks and limits

The native suite passed 14 tests and the core-only suite passed 13. Both skipped
the generic local binder scan because its complete default source map was not
configured; the nine W14 bindings were verified explicitly. Clang-format 19 and
gersemi 0.22.2 passed. All 17 reconstruction GPU fixtures passed, including blue
despill and raw Hint view, as did exact green/blue chroma shader checks.

The live screenshots cover a green screen. Blue graph parity and blue despill
were tested numerically; a real blue-screen clip was not available for a visual
check. The optional CK2 hint was exercised with DirectML, and still uses host
bilinear resizing around resident sessions on NVIDIA. Transparent materials
need broader footage evaluation. CK1's learned refiner remains disabled.

The properties can briefly report Busy while a working pipeline has an
outstanding frame. The final NVIDIA properties sample had that status alongside
the measured 1.04 ms hint and 16.8 ms delay; the frame logs and source render
confirm continued keying. This status wording is a shared UI follow-up.

The hidden test host was terminated after filter removal through OBS's API.
This run does not establish a clean whole-OBS shutdown or a long soak result.
The test websocket server was disabled, its password cleared and the GPU lock
released. The installed OBS and its configuration were untouched.
