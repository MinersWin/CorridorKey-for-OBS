<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Performance

Watch **new keys/s** and **displayed age** in the filter status. OBS can render at
60 FPS while repeating an older keyed frame. Displayed age measures how old the
frame on screen is, including capture, inference, reconstruction and publication.

On Windows, an eligible NVIDIA GPU can keep CK2 tensors and refiner tiles on the
GPU through D3D11/CUDA transport. The status says which transport was selected.
TensorRT preparation can take minutes even with cached engines; OBS stays in
passthrough while it runs. Driver, runtime or graph changes can require a rebuild.

| Hardware/path | What to expect |
| --- | --- |
| RTX 5070 Ti | Facecam displayed about 56 distinct refined keys/s at 4K with 33.5 ms age in the controlled test. The 60 FPS target was not reached. See the measurements below. |
| Other NVIDIA RTX GPUs | Device transport requires OBS and inference to use the same adapter and a working CUDA/TensorRT runtime. Throughput and memory use need measurements on the specific card. |
| AMD/Intel on Windows | DirectML can keep CK2 tensors and refiner tiles on the GPU through D3D11/D3D12 sharing. If the device, shared textures or validation fail, it uses staged transfers. AMD and Intel hardware measurements are still needed. |
| Linux | This D3D11 transport is Windows-only. Linux uses its existing transport and provider selection. |

Start with Facecam and refinement off when testing a new GPU. Facecam uses a
256x512 model grid; Balanced uses 512x512, and Quality adds Stage 2. Refinement
uses 384, 512 and 768 tiles respectively. Increasing the tile budget covers more
edges and needs more memory and inference work.

If refinement lowers new keys/s or raises displayed age too far, disable it or
reduce its tile budget. A 4K source still adds native reconstruction work even
when the model grid is small. Test with the scene, encoders and other GPU effects
you intend to use while streaming.

If device transport cannot initialize, the filter uses staged transfers. You can
also select the staged transport in advanced settings to compare behaviour. A
backend failure may leave the source in passthrough with an explanation in the
status. Unknown driver and runtime versions are attempted; a version string alone
does not prevent the plugin from loading.

## Running two cameras

Each camera keeps its own preset, crop and temporal state, while active filters
take turns on the GPU. In a controlled RTX 5070 Ti test, 1080p Facecam plus 4K
Balanced, both with refinement, produced about 33 new keys/s each and used about
8 GiB of whole-device VRAM. Median displayed age was about 50 ms for Facecam and
66 ms for Balanced. See the [multi-instance results](../dev/multi-instance.md).

Watch the status of both filters with your complete streaming scene running.
Hiding a source releases its native frame buffers after pending work drains;
model sessions stay loaded for reuse when it becomes visible again. Engine
preparation can need additional temporary memory beyond steady operation.

## DirectML on AMD and Intel

Select DirectML and start with Facecam, refinement off. Auto transport checks
whether OBS and inference can share resources on the same adapter. Device
transport avoids analysis and model-output readback. The refiner also tries GPU
atlas sharing and can fall back separately; the log reports that fallback. On
laptops, make sure OBS uses the GPU you intend to run inference on.

Turn refinement on after checking displayed age. Staged tile transfers can make
it much slower even when the model itself runs quickly. Balanced doubles the
model grid area, and Quality adds a second model stage. Reducing the source
resolution can also lower reconstruction and tile-transfer cost.

DirectML uses FP32 model arithmetic. The FP16 experiment does not enable a new
user setting. Windows AMD and Intel support remains expected pending community
reports; measurements from DirectML on the RTX 5070 Ti do not predict either
vendor's performance. See [DirectML transport and results](../dev/dml.md).

For scale, DirectML on the RTX 5070 Ti produced these refined 1080p results:

| Profile | Staged keys/s / displayed age | Device keys/s / displayed age |
| --- | ---: | ---: |
| Facecam | 9.25 / 217 ms | 26.49 / 100 ms |
| Balanced | 6.80 / 317 ms | 20.34 / 133 ms |
| Quality | 4.83 / 496 ms | 10.84 / 201 ms |

This used one static source without an encoder and a 1000 ms diagnostic stale
limit. The normal limit is 250 ms, which can reject the slower staged results.
These are NVIDIA DirectML measurements; AMD and Intel users should measure their
own scene. The developer report includes memory use, refiner-off controls and
the qualification limits.

## Measured on the RTX 5070 Ti

With D3D11/CUDA transport on Windows, a 4K source produced these results:

| Profile | Refinement | Distinct keys/s | Displayed age | Whole-device VRAM peak |
| --- | --- | ---: | ---: | ---: |
| Facecam | Off | 58.0 | 16.8 ms | 4.38 GiB |
| Facecam | On | 56.0 | 33.5 ms | 5.03 GiB |
| Balanced | Off | 51.0 | 16.8 ms | 5.57 GiB |
| Balanced | On | 42.9 | 33.7 ms | 5.34 GiB |
| Quality | Off | 48.3 | 33.6 ms | 5.90 GiB |
| Quality | On | 40.4 | 66.8 ms | 7.00 GiB |

This was one static image on a 4K, 60 FPS canvas, without an encoder. Desktop
apps and allocations retained from earlier cases are included in the VRAM figures.
Separate launches would be needed to measure each profile independently. The
initial desktop baseline was 1.63 GiB. A moving camera, recording or streaming, and other filters
can change throughput and latency. The complete 1080p/4K comparison, including
staged transfers, is in the [developer results](../dev/interop.md#live-matrix).

Facecam with refinement used staged transfers at about 12 displayed keys/s and
161 ms age in the same test. If the status reports staged transfers, these
device-path rates do not apply.

For test conditions, precision, parity limits and measurements, see
[Windows interop](../dev/interop.md), [CK2 validation](../dev/ck2-pipeline.md) and
[refiner measurements](../dev/refiner.md). Those measurements are hardware and
workload specific. They are not guarantees for an entire GPU generation.
