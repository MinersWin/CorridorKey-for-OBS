<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Compatibility

Use OBS Studio 30.2 or newer on Windows x64, or a matching native Linux package
on Ubuntu 24.04/26.04 x86-64. The plugin uses the host's Qt libraries. Compatible
newer OBS and driver versions are tried; unfamiliar version numbers do not
disable the filter by themselves.

Windows testing covers an RTX 5070 Ti with NVIDIA and DirectML paths. AMD and
Intel DirectML await community reports. Linux GPU interop is not implemented,
and Linux live-host testing remains limited. Flatpak and HDR input are unsupported.

See the [tested and expected compatibility matrix](../compatibility.md) for
host versions, runtime evidence and known failures. A successful backend test
checks a small synthetic graph; verify your actual camera scene before streaming.
