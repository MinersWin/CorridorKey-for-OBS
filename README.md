<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CorridorKey for OBS

A native OBS Studio filter for green-screen keying with CorridorKey. Models run
on your computer; camera video is not uploaded. The plugin is free, with no paid
builds or tiers. Model licences are reviewed separately before download.

Version 0.1.0 is being prepared for its first release candidate. Windows keying
has been tested on an RTX 5070 Ti. Linux runtime testing and AMD/Intel community
validation remain release limitations. Check the [compatibility matrix](docs/compatibility.md)
before relying on it for a stream.

![CorridorKey model setup in OBS's default Yami theme](docs/images/first-run/yami-first-run.png)

## Features

- CorridorKey 2.0 with Facecam, Balanced and Quality presets, plus the learned Edge Refiner v15.
- Native matte reconstruction, foreground recovery, despill and crop controls.
- NVIDIA D3D11/CUDA and Windows DirectML D3D11/D3D12 device paths, with staged-transfer fallback.
- A model manager with licence review, resumable downloads, SHA-256 verification and offline import.
- Multiple filters with separate temporal state, live key rate and displayed-frame age.
- Optional automatic lip-sync and diagnostics you can review before sharing.
- Experimental CK1 green/blue keying with MODNet or CK2 hints under Advanced. See the
  [CK1 guide](docs/user/ck1.md) for quality comparisons and costs.

For a slower GPU, start with CK2 Facecam and Edge refiner off. CK1 has worse
edges in the evaluated clips. CPU inference is for diagnostics, not live use.
See [performance](docs/user/performance.md) for measured results and their limits.

## Quick start

1. Install a package from [Releases](https://github.com/MinersWin/CorridorKey-for-OBS/releases)
   with OBS closed. Windows needs OBS 30.2 or newer; choose the zip layout for
   your host if you use portable OBS. See [installation](docs/user/install.md).
2. Add **CorridorKey** to your source's **Effect Filters**. Keep Balanced selected.
3. Open **Manage models...**, choose **Set up CorridorKey**, and review the
   model licences. Accept them to download and prepare the package.
4. Install the separate edge-refiner package, or turn **Edge refiner** off. Missing
   packages or unavailable backends leave video unchanged with a status message.
5. Check the key on moving footage and crop away areas outside your screen.

Windows packages include ONNX Runtime and DirectML. Optional NVIDIA acceleration
has its own download and licence review. Linux packages target native OBS on
Ubuntu 24.04 or 26.04 with matching Qt libraries; Flatpak is not supported.
Model weights and derived model graphs are not included in the plugin packages.

Read the illustrated [first-run guide](docs/user/first-run.md),
[settings reference](docs/user/settings.md), [model manager guide](docs/user/model-manager.md),
[lip-sync guide](docs/user/lip-sync.md), [CK1 guide](docs/user/ck1.md),
and [troubleshooting](docs/user/troubleshooting.md). Changes and known limits are
in the [changelog](CHANGELOG.md).

## Build

The CMake infrastructure is based on the [official OBS plugin template](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc).
`buildspec.json` records reproducible SDK downloads with SHA-256 checks: OBS
30.2.0 and its matching OBS dependencies/Qt 6 for release builds, plus current
stable OBS 33.0.0 and 2026-10-23 dependencies for compile checks. These are build
inputs, not an exact-version runtime restriction. C++20, Qt and the frontend API
are enabled. You can also supply a compatible external SDK.

### Windows x64

Install Visual Studio 2022 Build Tools with Desktop development with C++, a recent
Windows SDK, and CMake 3.28 or later. The template downloads dependencies and builds
the OBS development libraries automatically. Use a normal PowerShell prompt:

```powershell
cmake --preset windows-x64 -DCORRIDORKEY_FETCH_ONNXRUNTIME=ON
cmake --build --preset windows-x64 --parallel
cmake --install build_x64 --config RelWithDebInfo --prefix "$PWD/stage"
```

With OBS closed, copy `stage/corridorkey` to
`C:/ProgramData/obs-studio/plugins/corridorkey`. Do not install into a running OBS.
The default install rules and Windows zip target OBS 33+: the module sits at
`corridorkey/corridorkey.dll`, beside `data/`. For OBS 30.2-32, use the `-legacy.zip`
asset, or configure with `-DCORRIDORKEY_WINDOWS_LAYOUT=LEGACY` to place the module
in `corridorkey/bin/64bit/`. CMake installs remove the alternate module copy.
The installer detects OBS in its default location and lets you choose the host
range for a custom installation. It installs one layout, removes obsolete module
copies from both ProgramData layouts and the default Program Files location,
and includes an uninstaller. Remove any custom-location older copies manually.
Neither package contains Qt, OBS or model weights. Packaged Windows releases
include the DirectML baseline runtime; a raw CMake install needs runtime assembly.

Do not install both zip layouts together. When upgrading manually, remove the
old `corridorkey.dll` before copying the new layout. In portable OBS 33+, use
`plugins/corridorkey/{corridorkey.dll,data/}` inside the portable installation.
Portable OBS 30.2-32 uses `obs-plugins/64bit/corridorkey.dll` and
`data/obs-plugins/corridorkey/`. The installer targets installed OBS, not portable
copies. See [OBS's migration guide](https://obsproject.com/kb/legacy-plugin-developer-info).

To compile-check the current SDK, use a separate build directory:

```powershell
cmake --preset windows-x64 -B build_current -DCORRIDORKEY_OBS_SDK=current
cmake --build build_current --config RelWithDebInfo --parallel
```

Release artifacts always use the baseline SDK. To supply another compatible SDK,
set `CORRIDORKEY_USE_BUNDLED_SDK=OFF` and `CMAKE_PREFIX_PATH` to its installation.

### Ubuntu 24.04 x86_64

Ubuntu 24.04 remains a supported build lane for compatible OBS 30.2-32 hosts
from the distribution/backports or the OBS PPA. The stock initial 30.0 package
is below the intended floor. CI builds the exact baseline SDK from source against
Ubuntu's FFmpeg libraries, independently of the installed OBS application:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config python3 \
  qt6-base-dev extra-cmake-modules libsimde-dev uthash-dev libjansson-dev \
  libx11-dev libx11-xcb-dev libxcb1-dev libxcb-xinput-dev libdrm-dev \
  libegl1-mesa-dev libgl1-mesa-dev libglib2.0-dev uuid-dev zlib1g-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
python3 tools/bootstrap-linux-sdk.py --sdk baseline
cmake --preset ubuntu-x86_64 -DCMAKE_PREFIX_PATH="$PWD/.deps/linux-sdk-baseline" -DCORRIDORKEY_FETCH_ONNXRUNTIME=ON
cmake --build --preset ubuntu-x86_64 --parallel
DESTDIR="$PWD/stage" cmake --install build_x86_64 --prefix /usr
```

Linux uses an installed or locally built OBS SDK rather than Windows dependency
archives, with no upper version limit. An already installed SDK 30.2+ may also be
used directly. For an optional current-SDK check, pass `--sdk current` to build
OBS 33 and minimal FFmpeg 8 into `.deps/linux-sdk-current`, then use that prefix.
This does not install an OBS host. OBS 33 itself dropped upstream Ubuntu 24.04
and Qt 6.4-6.9 support; its host needs a compatible newer distro/Qt stack. The
24.04 release lane does not advertise an OBS 33 host on the stock 24.04 stack.

### Optional inference dependency

`CORRIDORKEY_FETCH_ONNXRUNTIME` defaults to `OFF`. See
[the dependency notes](cmake/dependencies/README.md) for the pinned Linux CPU/CUDA
archives and the Windows DirectML source build. Enabling this prepares a runtime;
release runtime assembly is handled separately by `packaging/package.py`.

### Formatting and packages

Use clang-format 19.1.7 and gersemi 0.22.2. CI checks all tracked C/C++ and CMake
files. Release tags must match `v` plus `buildspec.json`'s version. A `v*` tag builds
Windows modern/legacy zips, an installer and Linux deb/tar.xz assets into a **draft** GitHub Release.
Release catalogue signing uses `CATALOGUE_SIGNING_KEY` in the protected `release`
environment. See the [release checklist](docs/dev/release-checklist.md) for RC
tags, signing approval and draft verification. Windows binaries are not code signed.

## Compatibility

Release builds use the OBS 30.2 API floor. OBS 30.2 and 33 have local Windows
evidence; 31 and 32 are expected to work. Compatible newer versions load with a
warning and capability checks. The plugin uses OBS's Qt and checks actual GPU
and runtime capabilities, without requiring an exact driver version.

The [compatibility matrix](docs/compatibility.md) separates host loading, graph
tests and live inference. Linux GPU interop is not implemented. AMD and Intel
DirectML await community reports; NVIDIA measurements do not qualify those GPUs.
The two-camera soak showed a positive memory trend that needs further investigation.

If something fails, [open an issue](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose)
with reviewed diagnostics from the model manager and the steps that reproduce it.

## Licensing

Plugin code is [GPL-3.0-or-later](LICENSE), with a
[section 7 additional permission](LICENSE-EXCEPTION.md) for the specified GPU
runtimes. Vendor libraries and separately downloaded models retain their own terms.
CorridorKey models use the Corridor Key Licence; their licence texts and attribution
are shown before downloading.

CorridorKey is by Corridor Digital LLC / Niko Pueringer. This is an independent
integration. See [third-party notices](THIRD_PARTY_NOTICES.md) for every bundled
component and model source.
