<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Binder validation, 29 October 2026

The native binder matched all 16 exporter reference files present for the final
run, from 01:18 to 01:21 UTC. Each graph passed both an expected SHA-256 check and
an independent chunk-by-chunk byte comparison. The local report also records the
exact manifest digest and checks that the exporter files did not change during
each comparison.

The source files were the original CK2 and foundation-statistics safetensors,
DeepMind RVM NPZ, the published refiner v12 safetensors, and the MoGe encoder-only
safetensors. All source sizes and SHA-256 pins matched the manifests. No downloads
were needed. All tested weights and manifests use float32; inference-time FP16
selection does not change these exporter bytes.

| Graph | Output bytes | Byte equality | Digest group |
| --- | ---: | --- | --- |
| `moge_256x512` | 351,049,804 | Pass | MoGe |
| `moge_512x512` | 351,049,804 | Pass | MoGe |
| `refiner_b256` | 93,520 | Pass | Refiner |
| `refiner_b384` | 93,520 | Pass | Refiner |
| `refiner_b512` | 93,520 | Pass | Refiner |
| `refiner_b768` | 93,520 | Pass | Refiner |
| `rvm_256x512` | 132,893,696 | Pass | RVM |
| `rvm_512x512` | 132,893,696 | Pass | RVM |
| `stage1_256x512` | 1,848,139,792 | Pass | Stage 1 |
| `stage1_512x512` | 1,848,139,792 | Pass | Stage 1 |
| `stage1nomask_256x512` | 1,848,139,792 | Pass | Stage 1 |
| `stage1nomask_512x512` | 1,848,139,792 | Pass | Stage 1 |
| `stage2_256x512` | 807,185,680 | Pass | Stage 2 |
| `stage2_512x512` | 807,185,680 | Pass | Stage 2 |
| `stage2nomask_256x512` | 807,185,680 | Pass | Stage 2 |
| `stage2nomask_512x512` | 807,185,680 | Pass | Stage 2 |

SHA-256 by group:

```text
MoGe     90a6078688cf8c53b1447a066c05c7bf58d91c0818924a199469e588b9048863
Refiner  05e61731b9d37c31dd870197bdd08783eab446abfd5a6afa6ad6db1d94f3656b
RVM      21b2f868e5b51297cc4803fa246219f808ea1cfc0bb1fb49c8e8918133709192
Stage 1  c0120dff1ad83d6f2417d33b75f5324c2f82f285072dc01f383c2abef00178e1
Stage 2  ab218724cc9428b1273ccce58d081ccd023ba7bed4e716600aea587fcbb32f5d
```

The initial directory inventory also contained four `stage1nomaskct` /
`stage2nomaskct` manifests. The exporter removed them before parity testing;
they are not part of these results. The final run used stable reference files
after a masked-stage export had finished rewriting its output.

## Checks run

Windows x64, MSVC 19.44, C++20, `RelWithDebInfo`, warnings treated as errors:

- Portable build with OBS and optional inference runtimes disabled: passed.
- Seven CTest suites: passed (`models`, `models_http`, `models_signature`,
  `binder_api`, `binder`, `runtime`, `contracts`).
- Binder fixtures: 21 Python test cases, including 96 deterministic mutations;
  two native API cases cover progress, cancellation and orphan recovery.
- A child process exited during binding without stack unwinding. It left no
  ready receipt; the next run repaired the partial output.
- Native catalogue install: accepted synthetic downloads bound successfully,
  passed package verification and became available through a reader lease.
- Missing private exports: local parity returned 77 and CTest reported a skip.
- CI's independent external-data Add graph generator: passed ONNX validation.
- Repository-wide clang-format 19.1.5 dry run and gersemi 0.22.2 check: passed.

The build emitted MSBuild's existing module-dependency warning for the two
`cache.cpp` basenames in the core target. It did not prevent compilation or tests.

## Remaining coverage

Linux/GCC execution remains for CI; this Windows machine has no WSL installation.
The real NPZ uses stored members. Deflate, ZIP64 directories and data descriptors
were covered with generated fixtures, not a multi-gigabyte publisher archive.
Tests simulate process interruption, not a physical power failure or failed disk.

The optional `refiner_b384` inference comparison through W06 was not run. Byte
equality establishes weight binding only; it does not establish provider support,
graph initialization or numeric inference parity. No GPU work or OBS testing was
needed for this package.

Large transposes that exceed the fixed read window can require repeated disk
reads. Current RVM transforms fit within the window. See [binder limits](binder.md#limits-and-publication)
before admitting a source revision with substantially larger transformed tensors.
