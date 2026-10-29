<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Edge Refiner v15

Catalogue sequence 3 ships Edge Refiner v15 with graph revision 5,
`121984ec124141b9a1e42af05946fafe26205fec`. CK2 remains pinned to revision 3;
CK1 and MODNet remain pinned to revision 4. The four tile budgets and tensor
interfaces are unchanged: `x[N,11,52,52]` produces `alpha[N,1,52,52]` and
straight-sRGB `fg[N,3,52,52]`.

The checkpoint is
[`corridorkey_edge_refiner_v15.safetensors`](https://huggingface.co/mantelmoritz/CorridorKey-Realtime-EdgeRefiner/blob/db3adf8820653508bc617ff9a1f994dc273d08cb/corridorkey_edge_refiner_v15.safetensors),
162,320 bytes, SHA-256
`b38746544edbdaa207723ffe1dd6e799e1fa7f176bd74ad4c7c91d5dd84abf74`.
The anonymous HF API listing and downloads agree on every graph, JSON and
checkpoint byte count and digest. Each ONNX file is 30,456 bytes; each exporter
JSON is 3,648 bytes. The model card recommends v15 and retains v12 as a previous
version. Both repositories' licence text still matches the bundled Corridor Key
Licence 2.0 record, so licence acceptance uses the same text hash.

The [inventory](catalogue-v1-inventory.md) lists all pins. The
[verification record](validation-r3-refiner-v15.json) includes the HF listing,
model-card hashes, native installation result and live log excerpts.

## Binding and updates

W05 bound all four graphs from the anonymously downloaded checkpoint. Each output
is 161,296 bytes with SHA-256
`0247c1a6af261187b4befb8fbed4023149063ab6945502c0411c62f4eab8f603`.
All four match the exporter's local revision-5 `.weights` files byte for byte.
The native catalogue service rejected installation without licence acceptance,
then imported, bound, published and acquired the accepted v15 package.

The package retains the ID `refiner-v12` to let W04 recognise an installed v12 as
an update. Its displayed name is Edge Refiner v15; the catalogue offers only the
revision-5 package. Saved version choices also resolve to v15. Installation
preserves the selected old revision. A successfully prepared replacement selects
v15. While the model manager is open, its disk refresh removes superseded refiner
revisions after their readers release their leases. Cleanup resumes when the
manager is reopened. The selected revision is protected from automatic removal.

Runtime tests publish and reuse a v12 cache, then check v15, a weights-only change
and a manifest-only change. None can reuse the old engine. A separate test creates
real ORT CPU sessions with identical synthetic graph bytes and the v12/v15 weight
identities: the old cache reuses its sentinel, the new identity gets another
directory, and subsequent v15 sessions reuse only the new cache. This exercises
the plugin's session cache handling without requiring TensorRT in ordinary CI.
The session test runs when `CK_TEST_BUNDLE` and `CK_TEST_GRAPH` are set; the
optional probe test also needs `CK_TEST_HELPER`.

Manager tests cover update discovery, installation without changing `current`,
explicit activation, an old reader blocking removal, cleanup after release and
protection of the selected v15 revision. The production catalogue test checks all
four v15 graph pairs and selects the correct refiner for each tile budget.

## Live check and scope

A dedicated portable OBS Studio 33.0.0 instance loaded the plugin built against
OBS 30.2. The test used an RTX 5070 Ti, DirectML with ORT 1.24.4 / DirectML 1.15.4,
a looping 1920x1080 reporter clip, a custom 512x512 analysis grid and 384 tiles.
The activation log recorded:

```text
Active refiner: Edge Refiner v15; revision 121984ec124141b9a1e42af05946fafe26205fec; manifest cbb6a1deae57bc0ae1f0ef0a629d2d5c8ab78c45f1202680725799720e134e9b
```

The run completed refined frames and logged 18 refiner samples. The GPU lock was
held during the OBS runs and released after the owned test process stopped.
The installed OBS and other test instances were untouched. The hidden test
process was terminated after the smoke check, so this run does not establish
clean shutdown behaviour. A Windows Firewall prompt prevented mouse inspection;
no firewall permission was granted. This is an activation and execution check.
It does not certify performance or full-pipeline quality. Earlier W13 captures
and quality measurements in [refiner.md](refiner.md) still describe v12.

The Windows plugin build and all 14 CI-selected CTest groups pass. The two optional
real-ORT session tests pass with 26 assertions. The refiner-only native binder
parity check passes for all four budgets. Clang-format 19 passes for all tracked
C/C++ files; gersemi 0.22.2 passes for all tracked CMake files. Linux execution,
TensorRT execution with v15 and full-sequence quality comparisons were not rerun
for this package.
