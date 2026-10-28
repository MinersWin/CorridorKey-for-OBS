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

- OBS Studio **33.0.0 or later** (current build ABI baseline).
- Windows 10/11 x64, or Ubuntu 24.04 x86_64 with a compatible OBS installation.
- The Qt 6 libraries provided by OBS on Windows, or the distribution on Linux.
- Future inference builds will require a supported GPU and separately downloaded
  models. No GPU runtime is required for this pass-through build.

Add **CorridorKey** to a source's video filters. The status line identifies this
build as pass-through. **Manage models...** remains disabled until the dialog exists.

## Build

The CMake infrastructure is based on the [official OBS plugin template](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc).
`buildspec.json` pins OBS 33.0.0 and the matching 2026-10-23 OBS dependency and Qt 6
archives, with SHA-256 checks. C++20, Qt and the frontend API are enabled.

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

### Ubuntu 24.04 x86_64

Install a compatible OBS development package, or build the pinned SDK as CI does.
The Ubuntu archive and the official OBS PPA currently provide older versions on
24.04. The SDK bootstrap builds OBS 33 and a minimal FFmpeg 8 dependency locally;
it does not install or replace your OBS application:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config python3 \
  qt6-base-dev extra-cmake-modules libsimde-dev uthash-dev libjansson-dev \
  libx11-dev libx11-xcb-dev libxcb1-dev libxcb-xinput-dev libdrm-dev \
  libegl1-mesa-dev libgl1-mesa-dev libglib2.0-dev uuid-dev zlib1g-dev
python3 tools/bootstrap-linux-sdk.py
cmake --preset ubuntu-x86_64 -DCMAKE_PREFIX_PATH="$PWD/.deps/linux-sdk"
cmake --build --preset ubuntu-x86_64 --parallel
DESTDIR="$PWD/stage" cmake --install build_x86_64 --prefix /usr
```

Linux uses an installed or locally built OBS SDK rather than Windows dependency
archives; CMake requires SDK version 33.0 or newer. The resulting package needs
a separately installed OBS 33 host on Ubuntu 24.04; the stock 24.04 package is
insufficient. Build against the oldest supported SDK when widening compatibility.

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
