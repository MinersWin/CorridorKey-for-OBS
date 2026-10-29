<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

## 0.1.0 (release candidate preparation)

First native CorridorKey release for OBS Studio 30.2 and newer.

- CK2 Facecam, Balanced and Quality presets with a learned edge refiner,
  guided matte reconstruction, foreground recovery, despill and crop controls.
- Windows NVIDIA D3D11/CUDA and DirectML D3D11/D3D12 device transport,
  with staged-transfer fallback when sharing is unavailable.
- Experimental CK1 green/blue models with MODNet, CK2 Stage-1 and Expert chroma hints.
- Model licence review, resumable downloads, hash verification, offline import,
  local weight preparation and a signed release catalogue.
- Multiple filter instances, live key rate and displayed-frame age, optional
  automatic lip-sync, and diagnostics with a preview before copying.
- Windows installer and modern/legacy zips; Ubuntu 24.04 and 26.04 deb/tar packages.
  DirectML is included on Windows; NVIDIA runtimes are separate accepted downloads.

Known limitations: Linux GPU interop is not implemented, Linux runtime coverage
is limited, and AMD/Intel DirectML await community testing. CK1's edge quality is
below CK2. The multi-instance soak showed an unresolved positive memory trend.
Windows binaries are not code signed and may trigger SmartScreen. Flatpak and
HDR input are unsupported. See the [compatibility matrix](docs/compatibility.md)
and [performance guide](docs/user/performance.md) before using a candidate live.
