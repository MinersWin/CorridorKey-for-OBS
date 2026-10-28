<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS

**Pre-alpha / work in progress. This build passes video through unchanged.**
Neural inference, model downloads and the model manager are not implemented yet.
The visible controls describe the planned interface and currently have no effect
on the image. Do not use this version for production keying.

A native OBS Studio video filter for neural background keying on Windows x64
and Linux x86_64. No model weights or derived graphs are included.

## Planned features

- CorridorKey 2.0 and CorridorKey 1.0, with Facecam 60, Balanced and Quality profiles.
- Optional learned edge refinement, despill and advanced controls in filter properties.
- In-OBS model management with licence acceptance, resumable downloads and SHA-256 verification.
- GPU inference through ONNX Runtime: NVIDIA TensorRT-RTX / TensorRT / CUDA,
  AMD MIGraphX where available, and DirectML as the Windows fallback.
- Live processing and input-to-output delay, with optional audio sync adjustment.

These are development goals, not capabilities of the current build. CPU execution
is intended for debugging. AMD support will be marked community-tested until
hardware validation is available.

## Requirements

- OBS Studio **30 or newer** is the intended compatibility range; see the matrix below.
- Windows 10/11 x64, or Ubuntu 24.04 x86_64 with a compatible OBS installation.
- The Qt 6 libraries provided by OBS on Windows, or the distribution on Linux.
- Future inference builds will require a supported GPU and separately downloaded
  models. No GPU runtime is required for this pass-through build.

Add **CorridorKey** to a source's video filters. The status line identifies this
build as pass-through. **Manage models...** remains disabled until the dialog exists.

## Build

The CMake infrastructure is based on the [official OBS plugin template](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc).
`buildspec.json` records reproducible SDK downloads with SHA-256 checks: OBS
30.0.2 and its matching OBS dependencies/Qt 6 for release builds, plus current
stable OBS 33.0.0 and 2026-10-23 dependencies for compile checks. These are build
inputs, not an exact-version runtime restriction. C++20, Qt and the frontend API
are enabled. Compatible external SDKs are supported.

### Windows x64

Install Visual Studio 2022 Build Tools with Desktop development with C++, a recent
Windows SDK, and CMake 3.28 or later. The template downloads dependencies and builds
the OBS development libraries automatically. Use a normal PowerShell prompt:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
cmake --install build_x64 --config RelWithDebInfo --prefix "$PWD/stage"
```

With OBS closed, copy `stage/corridorkey` to
`C:/ProgramData/obs-studio/plugins/corridorkey`. Do not install into a running OBS.
The release zip uses this same layout; the installer includes an uninstaller.
Neither package contains Qt, OBS, GPU runtimes or models.

To compile-check the current SDK, use a separate build directory:

```powershell
cmake --preset windows-x64 -B build_current -DCORRIDORKEY_OBS_SDK=current
cmake --build build_current --config RelWithDebInfo --parallel
```

Release artifacts always use the baseline SDK. To supply another compatible SDK,
set `CORRIDORKEY_USE_BUNDLED_SDK=OFF` and `CMAKE_PREFIX_PATH` to its installation.

### Ubuntu 24.04 x86_64

Use Ubuntu 24.04's OBS development package for the baseline build:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  qt6-base-dev libobs-dev obs-studio
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64 --parallel
DESTDIR="$PWD/stage" cmake --install build_x86_64 --prefix /usr
```

Linux uses an installed or locally built OBS SDK rather than Windows dependency
archives, with no upper version limit. For an optional current-SDK check,
`tools/bootstrap-linux-sdk.py` builds OBS 33 and minimal FFmpeg 8 into
`.deps/linux-sdk`, without replacing the host. Install its additional SDK build
dependencies listed in the script, then pass that prefix through `CMAKE_PREFIX_PATH`.

## Compatibility

| Platform / host | Intended support | Verification in this foundation |
| --- | --- | --- |
| Windows x64, OBS 30.x–32.x | Expected to work; release SDK baseline 30.0.2 | C++20 objects compiled against 30.0.2 headers with Clang; full MSVC build and runtime validation pending |
| Windows x64, OBS 33.x | Expected to work | C++20 objects compiled against 33.0.0 headers with Clang; current-SDK MSVC job configured; runtime validation pending |
| Ubuntu 24.04 x86_64, OBS 30+ | Expected to work | Baseline build job configured; first hosted run and runtime validation pending |
| Newer/unknown OBS releases | Load with a warning when the ABI remains compatible | Untested; retain pass-through if optional capabilities are unavailable |
| GPU providers, models, other distros | Planned; no inference in this build | Not tested |

The filter uses longstanding OBS APIs. It logs the host version; an unreviewed
version alone never causes rejection. Future optional APIs must be resolved at
runtime, and missing inference/provider capabilities must leave pass-through
available. A genuine host ABI break may still prevent loading before plugin
initialization. No exact Qt, ORT or GPU-driver version whitelist is imposed.
Build download pins are separate from runtime compatibility. Qt must have the
host-compatible ABI; future inference will negotiate ORT's versioned C API and
check actual provider/device capabilities before use.

Please [open an issue](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose)
with your OBS version, platform, GPU, driver, backend and OBS log when something
fails. Runtime support remains provisional until the corresponding matrix entry
records an actual test.

### Optional inference dependency

`CORRIDORKEY_FETCH_ONNXRUNTIME` defaults to `OFF`. See
[the dependency notes](cmake/dependencies/README.md) for the pinned Linux CPU/CUDA
archives and the Windows DirectML source build. Enabling this prepares a runtime;
it does not add inference to this filter or bundle that runtime in releases.

### Formatting and packages

Use clang-format 19.1.7 and gersemi 0.22.2. CI checks all tracked C/C++ and CMake
files. Release tags must match `v` plus `buildspec.json`'s version. A `v*` tag builds
Windows zip/installer and Linux deb/tar.xz assets into a **draft** GitHub Release.
The workflow uses the repository's automatic token; no custom secrets are needed.

## Licensing

Plugin code is **GPL-3.0-or-later**; see [LICENSE](LICENSE). A separate
[section 7 additional permission](LICENSE-EXCEPTION.md) permits linking or loading
specified proprietary GPU runtimes and drivers where the relevant copyright
holders grant it. It does not grant redistribution rights to vendor libraries.

**CorridorKey models are not included and are not covered by the plugin's GPL.**
Users must accept the applicable **Corridor Key Licence** before future downloads.
Credit: **Corridor Digital LLC / Niko Pueringer**. MoGe-2 is MIT; RVM is CC BY 4.0.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for sources and attribution.
This is an independent integration, not an official Corridor Digital product.
