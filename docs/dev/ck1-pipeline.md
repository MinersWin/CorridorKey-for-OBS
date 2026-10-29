<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CK1 pipeline and hints

CK1 uses `Pipeline` through `ModelPipeline`, the same worker admission,
generation, frame lease and publication contract as CK2. The CK1 worker admits
one outstanding frame/result, rejects duplicate sequence/tick submissions and
stops admission after three consecutive inference failures. Failed preparation
keeps the previous pipeline alive. Teardown runs on the control worker.

## Release and licensing

Graph revision 4 is `082f53c75d275c10b1ddf3be2cb72c5ca559f30b` in
`mantelmoritz/CorridorKey-ONNX-Graphs`. `ck1-green`, `ck1-blue` and `modnet`
each contain 256, 384 and 512 variants. CK1 defaults to 384. Checkpoint revisions,
sizes and SHA-256 values are in `data/models/sources-v1.json`. The hash fixture
is `tests/fixtures/ck1-release-r4.json`.

All 18 graph/JSON files were downloaded anonymously. ONNX hashes matched the
coordinator's revision-4 list and the HF LFS listing. JSON files matched the
HF listing's Git blob IDs; SHA-256 was then computed from their verified bytes.
All local files matched the published bytes. All three checkpoint sizes and
hashes matched the immutable HF API listing.

CK1 acceptance includes the full Corridor Key Licence v1 text, with its
additional terms. The record also explains that the HF card lists CC-BY-NC-SA-4.0.
The MODNet acceptance record includes the publisher's full Apache-2.0 LICENSE
and NOTICE at the pinned webcam conversion revision. Graph licences remain
separate. Exact text digests and source links are in the licence registry;
verification evidence is in [validation-w14-artifacts.json](validation-w14-artifacts.json).
No weights, graphs or upstream model implementation are stored in Git.

## Frame path

Capture squashes the manually cropped frame to the selected square grid. RGB
is planar sRGB in [0,1]. Normalization runs inside the graph. CK1 consumes
`rgb [1,3,S,S]` and `hint [1,1,S,S]`, and emits linear `alpha [1,1,S,S]` plus
straight sRGB `fg [1,3,S,S]`. There is no confidence input or output.

MODNet consumes that same RGB tensor at the same grid and emits `matte`.
CK2 Stage-1 hints reuse `ck2::SequenceState`, including MoGe caching, RVM state
and discontinuity resets. The released CK2 square path is 512x512. The CK1 grid
RGB is resized to 512; its half-grid alpha is resized back to the CK1 grid.
Both use the same capture identity and crop. A private synchronous provider
prevents dependency cycles and stale cross-source hints. Unimplemented hint
sources fail preparation. Every hint runs per frame; refresh/reuse is disabled.

Chroma runs `effects/ck1_chroma.effect` through an OBS graphics context. Its
screen channel is green or blue, with similarity and smoothness controls.
The staged implementation uploads the small captured RGB and reads back the
hint before inference. This Expert path includes a graphics synchronization
cost; it is not a zero-transfer native interop path.

The decoded maps and original retained input enter W08 reconstruction without
a CPU output upsample. Blue checkpoint selection also selects blue-channel
despill. Alpha/foreground/crop views use the existing reconstruction output;
Hint view samples the supplied hint directly, bypassing guided filtering and
output levels; crop and source-alpha bounds still apply. The learned
refiner stays off regardless of saved CK1 settings.

## Providers and activation

Each CK1/MODNet graph tries captured TensorRT FP16, uncaptured TensorRT FP16,
CUDA FP32, then DirectML FP32, restricted by the requested backend and the
compatible loaded ORT core. CPU is never a live fallback. Fixed bindings use
the CK2 persistent-buffer implementation. A capture preparation failure tries
the next alternative; a recoverable run failure retries without capture.
The optional CK2 hint retains its FP32 foundation policy and FP16 stage policy.

Production activation acquires the selected screen package and the chosen hint
package only after licence acceptance. The combined model retains both reader
leases. The preparation key includes the model, screen colour and hint source,
in addition to the existing grid/backend/revision fields. W10's interop and
NVIDIA provider files are unchanged. The shared `filter_input.cpp` changes
select the pipeline, acquire its hint dependency and publish its cost. The
shared status adds one optional last-frame hint time; it is included in total
inference rather than added to it twice.
The existing worker phase log uses `stage1_ms` for the complete hint and
`stage2_ms` for CK1. Its top-level MoGe/RVM columns are unused for CK1; a
CK2 hint's foundations are included in the hint time. The validation tool
reports these phases explicitly as `hint_ms` and `key_ms`.

Advanced reveals CK1 with an Experimental label and the CK2 Facecam fallback
recommendation. Chroma requires Expert and displays a teal/uneven-screen
warning. Pipeline banks remain independent. `ck1_refiner` remains false;
there is no evaluated CK1 refiner package.

## Validation tools

Build with ORT and `CORRIDORKEY_CK2_DEV=ON` for these local tools:

```text
corridorkey_ck1_validate CONFIG parity|chain|ck2hint SIZE ck1green|ck1blue|modnet REPORT.json BACKEND
corridorkey_ck1_catalogue DATA EXPORTS SOURCE_MAP.json CACHE REPORT.json
```

The parity tool reads every reference `in_*` and compares every `out_*` from
the exporter's read-only `.ref.npz`. It records 21 host-inclusive runs, graph
and weight hashes, precision, capture mode and provider partitions. The
chain mode feeds MODNet into CK1, using the matching reference RGB and matte.
The `ck2hint` mode exercises the full CK2 hint path and reports ranges and cost;
its hint differs from the exporter reference, so it makes no parity claim. The
catalogue tool uses a fresh test cache, verifies all local input blobs against
the production catalogue, checks missing-acceptance rejection, imports through
`CatalogueService`, runs the native binder, and acquires the verified leases.
Its licence acceptance is for local validation. Distributed builds have no
development override or validation tools.
