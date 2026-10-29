<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
CorridorKey 0.1.0 brings native green-screen keying to OBS Studio 30.2 and newer.
It includes CK2 Facecam, Balanced and Quality presets, a learned edge refiner,
crop and despill controls, multiple filter instances and optional automatic lip-sync.
Models run locally. The model manager reviews licences, downloads and verifies
files, and prepares weights on your computer.

Windows supports NVIDIA and DirectML device paths, with staged-transfer fallback.
CK1 green/blue with MODNet is available under Advanced as an experimental option.
For a slower GPU, try CK2 Facecam with refinement off first.

Install with OBS closed. Windows packages include ONNX Runtime and DirectML;
optional NVIDIA runtimes and all model packages require separate downloads and
licence acceptance. Use the modern zip for portable OBS 33+, or the legacy zip
for OBS 30.2-32. Linux packages are separate for Ubuntu 24.04 and 26.04 and must
match your native OBS/Qt installation. No model weights or derived model graphs
are included. The model catalogue is signed and verified by the plugin.

This is a release candidate. Linux GPU interop is not implemented, Linux runtime
coverage is limited, and AMD/Intel DirectML await community testing. CK1's edges
are worse than CK2's in the evaluated clips. The two-camera soak showed rising
memory use that needs further investigation. Flatpak and HDR input are unsupported.
Windows binaries are not code signed and may trigger SmartScreen.

Verify `SHA256SUMS.txt` and read the platform's notices and bundle inventory.
Follow the [installation guide](https://github.com/MinersWin/CorridorKey-for-OBS/blob/main/docs/user/install.md)
and check [compatibility](https://github.com/MinersWin/CorridorKey-for-OBS/blob/main/docs/compatibility.md)
before testing your full streaming scene. Report problems with reviewed diagnostics
from the model manager.

CorridorKey is by Corridor Digital LLC / Niko Pueringer. This free, independent
OBS integration uses the models under their separately accepted terms.