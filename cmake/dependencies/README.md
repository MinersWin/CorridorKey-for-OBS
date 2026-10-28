<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Optional ONNX Runtime scaffold

`CORRIDORKEY_FETCH_ONNXRUNTIME=ON` prepares **ONNX Runtime 1.31.1**. It is `OFF`
by default, including CI and release packages. The filter has no engine and does
not yet link the imported `CorridorKey::OnnxRuntime` target or package the runtime.
The download pin is for reproducibility, not an exact-version runtime whitelist.
Set `CORRIDORKEY_ORT_ROOT` to use another compatible installed SDK without a
download. The eventual engine must negotiate ORT's versioned C API, inspect EP
availability, and retain pass-through when a runtime or device is unsupported.

- **Windows x64 / DirectML:** official GitHub releases publish CPU and CUDA
  binaries, not DirectML binaries. This scaffold downloads the official pinned
  release source archive, verifies SHA-256, and builds its shared DirectML runtime
  through an external CMake project. It needs the VS toolchain, a recent SDK and
  network access for upstream's pinned build dependencies, including DirectML's
  separately licensed NuGet package. No vendor runtime is bundled by this plugin.
- **Linux x86_64:** choose `CORRIDORKEY_ORT_LINUX_PROVIDER=CPU`, `CUDA12` or
  `CUDA13`. Each official GitHub release archive is pinned with its published
  SHA-256 digest. CUDA variants require compatible user-installed CUDA/cuDNN
  libraries. CPU is the default for dependency development, not a performance
  promise for the eventual model pipeline.

```powershell
cmake --preset windows-x64 -DCORRIDORKEY_FETCH_ONNXRUNTIME=ON
cmake --build build_x64 --target corridorkey-onnxruntime --config Release
```

```sh
cmake --preset ubuntu-x86_64 \
  -DCORRIDORKEY_FETCH_ONNXRUNTIME=ON -DCORRIDORKEY_ORT_LINUX_PROVIDER=CUDA12
```

The intended provider order is NVIDIA TensorRT-RTX / TensorRT / CUDA, AMD
MIGraphX where supported, DirectML as the Windows fallback, and CPU for debugging.
Those providers are not interchangeable build switches in the published ORT
archives: TensorRT-RTX and MIGraphX integration need separate validation. Model
licence acceptance, device selection, provider discovery, inference and deployment
of runtime DLLs/shared objects will be added with the engine. Do not enable a
provider merely because a GPU vendor is detected.
