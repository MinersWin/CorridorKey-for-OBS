<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS: model catalogue and installation

Research date: 28 October 2026. All download sizes and hashes below are evidence, not a released manifest. Graph files, binding recipes and parity reports must arrive from the export workstream. No model was run for this design. Related: [architecture](ARCHITECTURE.md), [licence and download UI](UX_DESIGN.md), [implementation tasks](TASKS.md).

## Distribution and licence decisions

The project is **free forever: no paid tiers or paid builds**. The plugin is GPL-3.0-or-later with the [GPLv3 section-7 additional permission](../../LICENSE-EXCEPTION.md) for the specified GPU runtimes. That permission does not relicense model weights or permit copying non-GPL-compatible model code. Keep the generic runtime, binder and shaders in the GPL repository. Keep every CK-derived graph, learned constant, CK-specific binding recipe and model checkpoint in the separate HF model release under its applicable model licence. Public documentation uses the published licence texts as its basis.

Do not copy CorridorKey source, its monkeypatches, `fused_dw.py`, upstream color utility constants, or tangentstorm code into the plugin. The project's bridge/runner/Lua/refiner-code/fused-post sources can inform a port where their provenance permits it. Standard algorithms should have independent mathematical references and provenance. Maintain [third-party notices](../../THIRD_PARTY_NOTICES.md) covering plugin dependencies, models, graph origins and required attributions. Credit Corridor Digital / Niko Pueringer and retain “CorridorKey”.

**Licence versions are part of the model identity.** CK2 and the published v12 refiner carry **Corridor Key Licence v2.0**, based on CC BY-NC-SA 4.0 with additional conditions. This includes conditions for redistribution/integration and a seat requirement for qualifying organisations above US$20m annual revenue. Show the full text and a restrained factual summary; never label the package simply “open source” or universally unrestricted for commercial use. [Pinned CK2 licence](https://huggingface.co/nikopueringer/CorridorKey2.0/blob/cf44a8db3f024bb49fd59daa408848ccf05f7477/LICENSE), [pinned refiner licence](https://huggingface.co/mantelmoritz/CorridorKey-Realtime-EdgeRefiner/blob/6e37255904c81ab224b1a1b39cbe4512c7a6dafb/LICENSE)

CK1's GitHub `LICENSE` is **Corridor Key Licence v1.0**, not plain CC BY-NC-SA. Its HF card lists only `cc-by-nc-sa-4.0` and has no separate licence file in the inspected repository. Record both pieces of evidence and show v1 additional terms conservatively; resolve the weight-specific catalogue wording before release. Do not silently apply CK2 v2 to CK1. Keep the free-project scope and distribution boundaries above; use the published licence texts as the basis for acceptance. [CK1 licence](https://github.com/nikopueringer/CorridorKey/blob/main/LICENSE), [pinned HF card](https://huggingface.co/nikopueringer/CorridorKey_v1.0/blob/f6386ddf042d8e92aeb5fd16cb9b101cff508195/README.md)

## Verified publisher artifacts

HF revisions and LFS SHA-256 values were read from the public HF model API with `blobs=true`. They have **not** been independently verified by downloading the multi-gigabyte files in this task. The RVM file already present in the research checkout was hashed locally; its MD5 matches the publisher's live `x-goog-hash` response.

| ID / original publisher | Immutable revision | File | Bytes | Licence evidence |
|---|---|---|---:|---|
| `ck2-main`, `nikopueringer/CorridorKey2.0` | `cf44a8db3f024bb49fd59daa408848ccf05f7477` | `corridorkey_v2.safetensors` | 2,661,698,720 | Corridor Key v2, repository licence |
| `ck2-stats`, same | Same | `foundation_stats.safetensors` | 18,864 | Same; learned statistics are weights |
| `moge2-vitb`, `Ruicheng/moge-2-vitb-normal` | `ca5f0e07ff01d3e5a364c1d954ed12ee1814b368` | `model.pt` | 419,110,160 | MIT model card; Microsoft MoGe code MIT, DINOv2 notices retained |
| `rvm-mae-small`, Google DeepMind | Fixed checkpoint filename; immutable identity is SHA-256 below | `pretrain_rvm_small16_256_204031069.npz` | 270,086,372 | Code Apache-2.0; non-code materials CC-BY-4.0 |
| `edge-v12`, `mantelmoritz/CorridorKey-Realtime-EdgeRefiner` | `6e37255904c81ab224b1a1b39cbe4512c7a6dafb` | `corridorkey_edge_refiner_v12.safetensors` | 94,336 | Corridor Key v2 |
| `ck1-green`, `nikopueringer/CorridorKey_v1.0` | `f6386ddf042d8e92aeb5fd16cb9b101cff508195` | `CorridorKey_v1.0.safetensors` | 398,849,256 | HF CC-BY-NC-SA-4.0 metadata; GitHub v1 additional terms, discrepancy above |
| `ck1-blue`, `nikopueringer/CorridorKeyBlue_1.0` | `51e6ccaa4b703f54be20a72ac2c37784fb9ba1cd` | `CorridorKeyBlue_1.0.safetensors` | 398,849,256 | Same scope question; separate checkpoint, not channel swapping |

| Artifact | SHA-256 |
|---|---|
| CK2 main | `c43596de98cc8866d577c8a9f9ea239acdf3f7a130ff24e25db4375a50590436` |
| CK2 stats | `924783ceddd293fd470126332ce328166faea6c4c8d2f36402d5b95187166af3` |
| MoGe | `16b8110e86d5dc5a849db120ca96ef3a223fd30b0c9146d1d81db504073da5f6` |
| DeepMind RVM | `8821ba9f5d983b9a3746bb5fe86b9c2ea834fe9985ab7b02438b917438bf2ede` |
| Refiner v12 | `377d7c60ab7f6a5691e9977a491bcea397ade1421bb6087f2bc79ad3a9ce315d` |
| CK1 green | `74d614f7d92fc559a118c30a7deadedc3cacd8ef83dcb85a030d0bed7af8b20b` |
| CK1 blue | `43bc5f6a08a9e5effe5d633d0d84bb0aff91037b35ab85d16cd812b38c5cac23` |

Metadata sources: [CK2 API](https://huggingface.co/api/models/nikopueringer/CorridorKey2.0?blobs=true), [MoGe pinned API](https://huggingface.co/api/models/Ruicheng/moge-2-vitb-normal/revision/ca5f0e07ff01d3e5a364c1d954ed12ee1814b368?blobs=true), [refiner API](https://huggingface.co/api/models/mantelmoritz/CorridorKey-Realtime-EdgeRefiner?blobs=true), [CK1 green API](https://huggingface.co/api/models/nikopueringer/CorridorKey_v1.0?blobs=true), [CK1 blue API](https://huggingface.co/api/models/nikopueringer/CorridorKeyBlue_1.0?blobs=true).

MoGe evidence: [publisher model card](https://huggingface.co/Ruicheng/moge-2-vitb-normal/blob/ca5f0e07ff01d3e5a364c1d954ed12ee1814b368/README.md), [Microsoft MoGe licence](https://github.com/microsoft/MoGe/blob/main/LICENSE). RVM evidence: [DeepMind licence statement](https://github.com/google-deepmind/representations4d#license-and-disclaimer), [original checkpoint](https://storage.googleapis.com/representations4d/checkpoints/pretrain_rvm_small16_256_204031069.npz). Publisher MD5: `ZJb6kj8+tasiJbVeplXu7Q==`; use SHA-256 for installation, not MD5 alone.

The common CK2 source downloads total approximately **3.12 GiB**, before graphs, bound data, engine cache and temporary space. Do not advertise that as total installed size. The reported 738-tensor CK2 count must be revalidated against the pinned safetensors header by the exporter. The published refiner model card contains older timings; the 28 October research acceleration measurements supersede those timings, not its licence/architecture data.

## Manifest schema

Decision: use a versioned, strictly validated JSON manifest on HF, signed by the project release key. The shipped catalogue pins a manifest digest/revision; updates are explicit. Signatures authenticate the catalogue, while file SHA-256 validates every artifact. No plugin execution path accepts arbitrary Python, downloaded DLLs or unbounded transformation programs from a model manifest.

This **structural example** cannot be installed. Angle-bracket placeholders are rejected by release validation. It shows one graph; a real package lists the complete dependency DAG.

```json
{
  "schema_version": 1,
  "id": "corridorkey2-sdr-v1",
  "revision": "<immutable-package-id>",
  "pipeline": "ck2",
  "graph_contract": 1,
  "min_plugin": "<release-version>",
  "licenses": [{
    "id": "LicenseRef-Corridor-Key-2.0",
    "applies_to": ["stage1", "ck2-main", "edge-v12"],
    "text_url": "<pinned-publisher-licence-URL>",
    "sha256": "<64-hex>",
    "requires_acceptance": true
  }],
  "weights": [{
    "id": "ck2-main",
    "repo": "nikopueringer/CorridorKey2.0",
    "revision": "cf44a8db3f024bb49fd59daa408848ccf05f7477",
    "path": "corridorkey_v2.safetensors",
    "format": "safetensors",
    "bytes": 2661698720,
    "sha256": "c43596de98cc8866d577c8a9f9ea239acdf3f7a130ff24e25db4375a50590436"
  }],
  "graphs": [{
    "id": "stage1",
    "repo": "mantelmoritz/<graph-repository-to-be-created>",
    "revision": "<HF-commit>",
    "path": "ck2/512x512/stage1.onnx",
    "sha256": "<64-hex>",
    "bytes": 0,
    "grid_hw": [512, 512],
    "opset": 18,
    "precision": "fp16",
    "inputs": "<typed-tensor-contract-reference>",
    "outputs": "<typed-tensor-contract-reference>",
    "binding_recipe": "binding/stage1.json",
    "binding_recipe_sha256": "<64-hex>",
    "depends_on": ["ck2-main", "ck2-stats"],
    "ep_compatibility": {"directml": "unverified", "cuda": "unverified"}
  }],
  "profiles": {"balanced": {
    "grid_hw": [512, 512], "roi": true,
    "stage2_interval": 0, "refiner_tiles": 512
  }}
}
```

Required schema fields beyond this abbreviated example:

- Package: display name, publisher, description, source URLs, source-code/export revisions, date, licence records with **separate code and weight scope**, attribution text, runtime bundle compatibility and signed catalogue identity.
- Each graph: byte length, SHA-256, role, actual ONNX IR/opset, precision, static input/output names/shapes/layout/dtypes, mathematical color/range contract, optional vs required status, state tensors/reset values, external-data table, recipe digest and parity report digest. Dynamic source size is a shader concern; static graph sizes are explicit.
- Dependency entries: exact HTTPS publisher URLs or HF repository/revision/path, expected size/hash, format, publisher-approved mirrors, licence reference, archive limits. Include foundation statistics and every derived/folded learned initializer.
- Compatibility entries: OS/architecture ranges, minimum required API/features, compatible EP/core/runtime ranges, vendor driver minimum, graph variant, tested precision, attention rank restrictions, CPU partition policy, test status (`unverified`, `expected`, `build-only`, `hardware-tested`, `community-tested`), dated exact test anchors. A tested version is not an exact-version allowlist. Unknown newer combinations are probed; only proven incompatibility disables a backend. Availability is not successful inference.
- Profiles: complete settings version and defaults, H×W grid, ROI behavior, stage intervals, model/hint/refiner dependencies, tile input/core/halo/batch and memory/build estimates. Units must be explicit. No “512p” ambiguity.
- Resource budgets: source download bytes, materialized bytes, peak conversion workspace, estimated cache bytes by backend, peak resident memory and an uncertainty margin. Unknown estimates are labelled, not zero.
- Future hint: `role: hint`, `semantics: screen_foreground` or `person`, RGB input and linear mask output, optional named recurrent state, native grid, refresh policy and explicit preprocessing. No CK2-only assumptions in the catalogue loader.

Reject unknown required schema versions, duplicate IDs, cyclic dependencies, NaNs, unsupported precision, invalid offsets, overlapping destinations, external-data path traversal and graph references outside the package. JSON integers for offsets/lengths must survive 64-bit handling; never pass them through a lossy double conversion.

## Compatibility records and admission

Keep runtime ranges separate from immutable artifact identities. A graph hash, upstream weight revision and binding recipe are exact by design; an OBS version or GPU driver is not an exact-match install token. Reference the broader host/provider matrix in [ARCHITECTURE](ARCHITECTURE.md), and store both required features and dated test evidence in each graph entry.

| Package / backend | Evidence now | Admission behavior |
|---|---|---|
| CK2 / CUDA, TensorRT-RTX, DirectML, MIGraphX | No exported CK2 ORT package tested in this design | Exporter parity plus local session/output probe required; do not mark ready from provider enumeration alone |
| CK1 / DirectML | Third-party design demonstrates a route; our independent export is untested | Consume our static-shape/rank-four graph and its parity evidence; third-party success is not our certification |
| Refiner v12 / each EP | PyTorch research evidence; native ORT path untested | Admit only declared static tile shapes and verified color/halo contract |
| Newer OBS, driver, core or runtime | Untested until a report exists | Resolve APIs, negotiate provider ABI and probe; retain an advisory if successful, fallback if an actual requirement fails |

For example, the current NVIDIA matrix documents RTX EP 0.5 with ORT 1.25.0-1.30.0 and TensorRT-RTX 1.7.x.x. Treat that as vendor range evidence, not a reason to refuse an otherwise compatible newer core before negotiation. Store driver lower bounds per runtime bundle, not per model weight file. [NVIDIA provider matrix](https://github.com/NVIDIA/TensorRT-RTX-EP-ABI#test-matrix)

## Local weight binding

**Choose deterministic local materialization into ordinary ONNX external-data blobs.** Directly pointing an ONNX initializer at bytes in safetensors is legal in principle: its absolute offset is `8 + header_length + tensor_relative_offset`, and the ONNX `location`, `offset`, `length` fields can describe that range. It works only when dtype, order, shape and contiguous storage already match. It cannot cover arbitrary transpose, precision conversion, fused weights, compressed NPZ members or PyTorch tensor views. [ONNX external data](https://onnx.ai/onnx/repo-docs/ExternalData.html)

Implement one generic C++ binder, with a small allowlisted recipe language generated by the exporter: read tensor/storage slice, reshape, transpose, concatenate, pad with zero, cast with defined rounding, and write aligned bytes. Reject executable expressions and unknown operations. Initially avoid expensive constant folding that would require recreating model math in the binder; let ORT optimize the bound graph locally. If a fold is necessary, the export recipe and independent parity fixture must account for every source byte.

1. Verify publisher file hashes before parsing. The recipe is tied to those exact hashes, graph hash and binder version.
2. Safetensors: bounded JSON header, checked dtype/shape/products and tensor spans. No generic deserialization hooks.
3. **MoGe `.pt`:** it is a PyTorch ZIP archive. The exporter supplies an authenticated storage map naming ZIP members, storage offsets, dtypes, shapes and strides. The C++ binder reads those byte ranges and applies declared views. It **never executes or interprets arbitrary `data.pkl` opcodes**. If a new publisher revision changes storage layout, require a new reviewed recipe. Do not embed libtorch merely to load a checkpoint.
4. **DeepMind RVM `.npz`:** parse the ZIP container and bounded `.npy` headers. Support the documented numeric dtypes, endian conversion, C/Fortran order and exporter-declared transposes; reject object arrays/pickle, archive traversal, oversized expansion and inconsistent shape/length. The extension is not a safetensors alias.
5. Write `weights.bin.partial` and the locally bound ONNX under a generation directory. Hash each output, verify total initializer coverage and deterministic expected digests, then atomically rename after success. Streams/chunks keep RAM bounded; no second in-memory copy of the entire CK2 file.
6. Validate graph loading and run known test tensors in the backend probe before marking ready. Keep original publisher blobs for repair/rebinding, deduplicated by content hash. Show their disk cost separately.

No Python is installed or needed on a streamer's machine. Python remains an **export/build-time** tool owned by the research/export workstream. The exporter must supply a small nonrestricted synthetic fixture for testing the binder in public CI. Weight-free means all learned values are external, including folded constants and foundation statistics; a tiny graph file is not proof of that property.

Optimization later: read compatible safetensors spans directly to avoid duplicate disk data. It must be observationally identical to the materialized layout, preserve safe relative paths, and pass on all EPs. Do not make this optimization the initial correctness path.

## Downloads, cache and updates

Use `obs_module_config_path` to locate the plugin cache root. Typical Windows path is `%APPDATA%/obs-studio/plugin_config/corridorkey/`; Linux is under OBS's user config, normally `$XDG_CONFIG_HOME/obs-studio/plugin_config/corridorkey/`. Resolve through OBS, not hard-coded assumptions. Allow an external cache location through the model manager. Never store weights in the installation directory or source repository.

Suggested directories: `catalog/`, `licenses/`, `blobs/sha256/`, `packages/<id>/<revision>/`, `bound/<recipe-hash>/`, `engines/<cache-key>/`, `partial/`, `receipts/`. The acceptance receipt records model/package revision, exact licence-text hashes, timestamp and local user context; it is not uploaded. Two filters share one download and one build per cache key. Use process locks and reader leases so deletion cannot invalidate an active session.

HF URL: `https://huggingface.co/{publisher}/{repo}/resolve/{commit}/{path}`. Follow HTTPS redirects, including the publisher's storage/CDN, but do not treat an ETag as a guaranteed SHA-256 or commit identity. Public models need no account/token. Gated models stay unsupported until a separate authenticated download design exists. [HF download documentation](https://huggingface.co/docs/hub/models-downloading)

Download policy:

- Retrieve small catalogue/licence metadata first. No graph/weight payload starts before acceptance. Show the dependency list, publisher, download size, estimated final/peak disk use and destination together.
- Stream to a `.partial` file; resume using HTTP Range and an origin validator. Require correct 206 `Content-Range`. If the server ignores Range or the validator changed, restart that file safely. Persist progress and recompute the final SHA-256, including resumed bytes.
- Use bounded concurrency (two file downloads by default), backoff for transient errors and 429, finite timeouts, cancellation and readable progress in MiB/GiB. Unknown totals use an indeterminate bar. Do not show an invented ETA.
- Check free space on the **actual destination volume**, including partial files, all new bound blobs, temporary conversion, engine cache, rollback revision and a margin of max(1 GiB, 10%). Recheck before conversion/build. The raw 3.12 GiB CK2 total is insufficient as a space check.
- Mirrors must deliver the same pinned bytes and be publisher-approved or user-selected; never switch to an unrelated community checkpoint. Offline import accepts a known artifact/package bundle, verifies all hashes and licences, and performs the same local binding. Import does not bypass acceptance.
- Model updates are optional and side-by-side. Never update automatically during streaming, silently change a profile or replace a working model. Changed licence hashes require acceptance again. Roll back by selecting the previous verified revision.
- Removing a model shows active users and exclusive vs shared bytes. Remove shared blobs only after the last reference is gone; clear build caches independently. Closing the manager leaves an explicitly started download running; Cancel stops it. Uninstalling the plugin offers a separate cache-retention choice.

Prefer Qt Network and Qt's hash/JSON facilities already available with the OBS UI; do not add curl solely for this task. The ZIP reader and signature library need a recorded dependency/licence review before selection. Use public hosted build runners only.

## CK1 input and hint contract

Upstream code is [nikopueringer/CorridorKey](https://github.com/nikopueringer/CorridorKey); the network is GreenFormer under `CorridorKeyModule/core`, with preprocessing in [inference_engine.py](https://github.com/nikopueringer/CorridorKey/blob/main/CorridorKeyModule/inference_engine.py). Green weights are at **`nikopueringer/CorridorKey_v1.0`**, not a guessed `nikopueringer/CorridorKey` HF path. Blue uses the distinct repository listed above.

The inspected inference contract resizes sRGB RGB and a linear [0,1] alpha hint to the same square model size, applies ImageNet RGB normalization, then concatenates them into **N×4×H×W**. The mask is not ImageNet-normalized. Outputs are `alpha` (linear coverage) and `fg` (straight sRGB). Upstream defaults are offline/high resolution; smaller static 256/384/512 grids need export and quality validation. The plugin-side public contract should be RGB [0,1] plus mask [0,1], with model-specific normalization packaged in the graph to avoid duplicating publisher code/constants.

The hint gives a rough foreground region and need not resolve individual hairs. The revision-4 owner decision selects MODNet by default. Chroma remains available in Expert for experiments with props, with a warning about teal objects and unevenly lit screens. Semantic portrait masks can remove those objects; show a Hint debug view and explain the tradeoff. No hint-quality scalar is a native CK1 input: a CK1 “strength” control, if offered, changes the supplied mask composition and must be labelled as preprocessing.

## Hint model candidates

Costs below are **planning envelopes**, not measurements: additional batch-1 inference at approximately 256-512 input pixels on a modern discrete GPU, excluding transfer and CK1. Actual ORT/EP timings must be collected. The listed downloads are free; licence obligations still apply. A segmentation mask is not final alpha.

| Candidate | Code licence evidence | Weight licence evidence | Expected added cost / decision |
|---|---|---|---|
| **MODNet photographic portrait** | Apache-2.0 | **Verified:** current publisher README explicitly covers code, models and demos, excluding GIFs | Plan 2-8 ms at a tested 512-ish grid. **First optional hint:** simple RGB-to-alpha contract and existing ONNX export route. Portrait semantics may lose props. |
| **MediaPipe SelfieSegmenter**, square/landscape | Apache-2.0 in Google repository | **Verified:** Google's original model card states Apache-2.0 | Plan 1-4 ms desktop GPU after conversion, low confidence; 256×256 or 144×256. **Second option if needed:** low cost, coarse person mask. Conversion from original TFLite to ONNX must pass parity; no TFLite runtime added by default. |
| **BiRefNet / BiRefNet_lite** | MIT | **Verified:** publisher HF cards label the checkpoints MIT | Plan 10-40 ms for lite at reduced resolution; full/high-res variants can be tens to hundreds of ms. Better object scope than portrait models, too costly as the default hint. Benchmark lite only after cheaper options. |
| **Robust Video Matting**, PeterL1n, MobileNetV3 | **Verified GPL-3.0**; treat code as GPL-3.0-only unless its grant proves “or later” | Official release supplies pretrained ONNX/PTH under repository distribution; downstream OBS sidecar explicitly GPL-3.0-only. **Weight-specific upstream grant was not independently explicit in inspected text; confirm before catalogue admission.** | Plan 2-8 ms at reduced analysis resolution; recurrent state must be per camera. Compatible direction for GPLv3 project, but not permissive; optional later. This is not DeepMind RVM. |
| **PP-Matting / PP-MattingV2** | **Verified Apache-2.0**, PaddleSeg | Publisher download links found, but **checkpoint-specific licence scope unverified** in inspected matting docs | Plan 2-10 ms depending on variant. Defer until weight licence and Paddle-to-ONNX parity are pinned. Do not infer weight terms solely from code licence. |
| **SAM 2.1 Hiera tiny** | Apache-2.0 | **Verified:** Meta explicitly includes model checkpoints | Plan 10-40+ ms including image encoder at native model input; memory/video state and prompts add complexity. Not a first-run hint. Possible future manually prompted workflow. |
| **EfficientSAM-Ti** | Apache-2.0 | **Verified repository scope:** official weights are distributed inside the same Apache-licensed repo; record the exact weight revision | Plan 5-25+ ms for encoder plus prompt decoder. Needs point/box selection or additional logic; not a replacement for automatic person detection. Defer. |

Sources:

- [MODNet current licence statement](https://github.com/ZHKKKe/MODNet#license), [official pretrained model instructions](https://github.com/ZHKKKe/MODNet/tree/master/pretrained). The old [2020 licence discussion](https://github.com/ZHKKKe/MODNet/issues/25) describes superseded terms; pin the current licence and model lineage instead of citing it as current.
- [MediaPipe source licence](https://github.com/google-ai-edge/mediapipe/blob/master/LICENSE), [original weight model card, p.1](https://storage.googleapis.com/mediapipe-assets/Model%20Card%20MediaPipe%20Selfie%20Segmentation.pdf), [Google model variants](https://developers.google.com/edge/mediapipe/solutions/vision/image_segmenter#models). Google's Pixel 6 task timings are not desktop ORT benchmarks.
- [BiRefNet code licence](https://github.com/ZhengPeng7/BiRefNet/blob/main/LICENSE), [full model card](https://huggingface.co/ZhengPeng7/BiRefNet), [lite model card at inspected revision](https://huggingface.co/ZhengPeng7/BiRefNet_lite/blob/aa62cd87eafb9cc43056d08ef3615a14628b831d/README.md).
- [Robust Video Matting code and downloads](https://github.com/PeterL1n/RobustVideoMatting), [official release](https://github.com/PeterL1n/RobustVideoMatting/releases/tag/v1.0.0), [downstream artifact notice, corroboration only](https://github.com/royshil/obs-backgroundremoval/blob/b2786e87dc1d08ed2f2ce335000a8fb1992dc944/data/models/rvm_mobilenetv3_fp32.onnx.license).
- [PaddleSeg licence](https://github.com/PaddlePaddle/PaddleSeg/blob/release/2.10/LICENSE), [PP matting model documentation](https://github.com/PaddlePaddle/PaddleSeg/tree/release/2.10/Matting).
- [SAM2 explicit code/weight licence](https://github.com/facebookresearch/sam2#license), [tiny checkpoint origin](https://dl.fbaipublicfiles.com/segment_anything_2/092824/sam2.1_hiera_tiny.pt).
- [EfficientSAM licence](https://github.com/yformer/EfficientSAM/blob/main/LICENSE), [official weight directory](https://github.com/yformer/EfficientSAM/tree/main/weights).

The revision-4 owner decision makes MODNet the default CK1 hint. CK2 Stage-1 alpha is the other normal hint option and shows its added cost; chroma is Expert-only. Add MediaPipe only if measured runtime savings justify its conversion and maintenance. Do not require both CK1 and CK2 downloads to use CK1.

## Future distilled hint slot

Reserve `hint.corridorkey-distilled` in the schema, not a fake installable model. Input: sRGB grid plus optional recurrent tensors; output: coarse foreground confidence including objects in front of the screen. Manifest declares semantics, temporal reset policy, input grid, latency/memory evidence and full training/teacher provenance.

A model trained on CorridorKey outputs must not be assumed permissively licensed. The existing refiner is explicitly distributed as a CorridorKey derivative. Determine the future model's rights from its dataset, teacher and licence conditions before marking a licence ID. Until weights and an evaluated graph exist, display no download button and make no quality/performance promise.

## Unresolved release inputs

1. Exported graphs, immutable graph repository, binding recipes, tensor/color contracts and numerical/sequence parity reports are pending; all EP compatibility for CK2 ORT is currently unverified.
2. W14 records the CK1 HF metadata discrepancy and accepts the full published v1 additional terms. Keep that disclosure with future revisions.
3. MODNet webcam is pinned in W14 with its LICENSE and NOTICE. Other hints still need exact revisions/hashes and verified checkpoint terms. A candidate table is not a released package allowlist.
4. Confirm native C++ ZIP/storage recipes against the exact MoGe/RVM artifacts; do not use runtime pickle as an emergency workaround.
5. Confirm CK1/refiner compatibility experimentally. The refiner's architecture can be hosted by both pipelines, but its CK2 training does not establish CK1 quality.
