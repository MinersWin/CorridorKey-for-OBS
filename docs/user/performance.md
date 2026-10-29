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
| AMD/Intel on Windows | The existing DirectML staged path remains available. Full-frame tile-atlas readback makes learned refinement substantially more expensive. Measurements for AMD and Intel hardware are not available. |
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
