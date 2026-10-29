<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party notices

The plugin is GPL-3.0-or-later with the [GPU-runtime permission](LICENSE-EXCEPTION.md).
Dependencies, documentation footage and separately downloaded models retain their
own licences. The permission does not grant redistribution rights to vendor libraries.

## Packaged software and host libraries

| Component | Licence and attribution | Delivery |
| --- | --- | --- |
| OBS Studio / libobs / frontend API | GPL-2.0-or-later; OBS Project and contributors. [Source](https://github.com/obsproject/obs-studio) | Supplied by the host; not bundled. |
| OBS plugin template | GPL-2.0-or-later, used here under GPL-3.0-or-later; OBS Project contributors. [Pinned source](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc) | Adapted CMake and formatting scaffolding. |
| Qt 6 Core and Widgets | LGPL-3.0 / GPL, or commercial licence; The Qt Company and contributors. [Obligations](https://www.qt.io/licensing/open-source-lgpl-obligations) | Dynamically linked; supplied by OBS on Windows or the distribution on Linux. |
| ONNX Runtime | MIT; Microsoft Corporation. [Licence](https://github.com/microsoft/onnxruntime/blob/v1.31.1/LICENSE) | Windows packages include the DirectML 1.24.4 core; Linux packages include the 1.31.1 CPU core. Packaging copies each archive's LICENSE and ThirdPartyNotices.txt. |
| DirectML | Microsoft DirectML software licence; Microsoft Corporation. [Official package and terms](https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4) | Windows packages include DirectML.dll 1.15.4, LICENSE.txt and ThirdPartyNotices.txt. |
| nlohmann/json 3.11.3 | MIT; Niels Lohmann. [Licence](https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT) | Header-based metadata parser compiled into the plugin; notice in `data/licenses/nlohmann-json.txt`. |
| zlib | Zlib; Jean-loup Gailly and Mark Adler. [Licence](https://zlib.net/zlib_license.html) | ZIP import; SDK/system library or pinned 1.3.2 fallback. Notices for 1.3.1 and 1.3.2 are in `data/licenses/`. |
| OpenSSL 3 | Apache-2.0; OpenSSL Project contributors. [Licence](https://github.com/openssl/openssl/blob/openssl-3.0/LICENSE.txt) | Linux distribution libcrypto for SHA-256 and catalogue verification; not bundled. Windows uses system CNG. |
| libcurl | curl (MIT-inspired); Daniel Stenberg and contributors. [Licence](https://curl.se/docs/copyright.html) | Linux distribution HTTPS transport; not bundled. Windows uses WinHTTP. |
| Inno Setup | Inno Setup licence; Jordan Russell and Martijn Laan. [Terms](https://jrsoftware.org/isinfo.php) | Windows installer engine; original notices retained in generated installers. |

The exact runtime archive URLs, hashes and selected files are in
`packaging/runtime-lock.json`. Each release supplies `BUNDLE_CONTENTS` and
platform-specific notices. Preserve those notices when redistributing packages.
No Qt, OBS, model weights or CorridorKey-derived graphs are included.

## Optional NVIDIA downloads

NVIDIA runtimes are downloaded from their publishers after a separate licence
review. They are not included in the plugin installer or hosted in this repository.
The recipes retain the full agreements and third-party acknowledgements.

| Component | Licence / source |
| --- | --- |
| ONNX Runtime GPU | MIT and bundled third-party notices; Microsoft. [Release](https://github.com/microsoft/onnxruntime/releases/tag/v1.31.1) |
| TensorRT-RTX execution provider | Apache-2.0 / MIT portions; NVIDIA. [EP licence](https://github.com/NVIDIA/TensorRT-RTX-EP-ABI/blob/v0.5.0/LICENSE) |
| TensorRT-RTX runtime and parser | NVIDIA TensorRT-RTX SLA and matching SDK acknowledgements. [Terms](https://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/reference/sla.html) |
| TensorRT 10 runtime | NVIDIA software licence agreement and TensorRT supplement. [Terms](https://docs.nvidia.com/deeplearning/tensorrt/latest/reference/sla.html) |
| CUDA runtime, cuBLAS, cuRAND, NVRTC and nvJitLink | NVIDIA CUDA EULA and embedded third-party notices. [Terms](https://docs.nvidia.com/cuda/eula/index.html) |
| cuDNN | NVIDIA SLA and cuDNN supplement, exactly as supplied by the publisher. [Terms](https://docs.nvidia.com/deeplearning/cudnn/backend/latest/reference/eula.html) |

See [packaging evidence](docs/dev/packaging.md) for the reviewed distribution
conditions, including the older cuDNN supplement shipped with newer binaries.
ROCm/MIGraphX is an integration target; no validated AMD runtime bundle is offered.

## Separately downloaded models

The model manager requires acceptance of the full texts and attribution before
downloading. Those model terms are separate from the plugin's GPL licence.
The release catalogue is signed with the release key and verified by the plugin.

| Model / source | Licence and attribution |
| --- | --- |
| [CorridorKey 2.0](https://huggingface.co/nikopueringer/CorridorKey2.0) | Corridor Key Licence v2.0: CC BY-NC-SA 4.0 plus Corridor Key Additional Terms. Corridor Digital LLC / Niko Pueringer; retain the CorridorKey name. |
| [CorridorKey 1.0 Green](https://huggingface.co/nikopueringer/CorridorKey_v1.0) and [Blue](https://huggingface.co/nikopueringer/CorridorKeyBlue_1.0) | The model cards list CC BY-NC-SA 4.0. Acceptance also includes the [published Corridor Key Licence v1 additional terms](https://github.com/nikopueringer/CorridorKey/blob/c8328d0d364e354c93920744f06e02f20a96cc8d/LICENSE). Corridor Digital LLC / Niko Pueringer; CorridorKey. |
| [MoGe-2 ViT-B encoder conversion](https://huggingface.co/mantelmoritz/MoGe-2-ViT-B-Encoder-safetensors/tree/3090937b74a09929079cf6c77ad1edd40a0a9aaf) | MIT; Microsoft Corporation and MoGe authors. Format conversion by Moritz Mantel from [Ruicheng/moge-2-vitb-normal](https://huggingface.co/Ruicheng/moge-2-vitb-normal/tree/ca5f0e07ff01d3e5a364c1d954ed12ee1814b368). The upstream DINOv2 Apache-2.0 notice credits Meta Platforms, Inc. and affiliates and is preserved with the MIT text. |
| [RVM: Recurrent Video MAE](https://github.com/google-deepmind/representations4d) | CC BY 4.0 for the non-code checkpoint; Google DeepMind and the Recurrent Video MAE authors. Checkpoint `pretrain_rvm_small16_256_204031069.npz`. This is distinct from Robust Video Matting. |
| [MODNet webcam conversion](https://huggingface.co/mantelmoritz/MODNet-Webcam-safetensors/tree/2b4102c4a84a919ed189b685f8d1089194eaa454) | Apache-2.0 LICENSE plus NOTICE. MODNet by Zhanghan Ke, Jiayu Sun, Kaican Li, Qiong Yan and Rynson W. H. Lau; format conversion by Moritz Mantel. [Upstream](https://github.com/ZHKKKe/MODNet/tree/28165a451e4610c9d77cfdf925a94610bb2810fb) excludes its GIF files from the code/model/demo grant. |
| [CorridorKey Realtime Edge Refiner](https://huggingface.co/mantelmoritz/CorridorKey-Realtime-EdgeRefiner) | The catalogue's v12 checkpoint uses the Corridor Key Licence v2.0. Refiner by Moritz Mantel, with CorridorKey attribution to Corridor Digital LLC / Niko Pueringer. |
| [CorridorKey ONNX Graphs](https://huggingface.co/mantelmoritz/CorridorKey-ONNX-Graphs) | Corridor Key Licence. Weight-free exports by Moritz Mantel; CorridorKey by Corridor Digital LLC / Niko Pueringer, with the underlying model notices retained. Graphs are downloaded separately and bound to accepted checkpoints locally. |

`data/models/licenses-v1.json` contains the complete acceptance texts, their
hashes and source URLs. `data/models/sources-v1.json` and the package manifests
pin source revisions and payload hashes. No upstream CorridorKey implementation
code is included in the GPL plugin.

## Build and test dependencies

- doctest 2.4.11: MIT, Viktor Kirilov. [Licence](https://github.com/doctest/doctest/blob/v2.4.11/LICENSE.txt).
  Test-only; not linked into the plugin.
- ONNX: Apache-2.0. [Licence](https://github.com/onnx/onnx/blob/v1.19.1/LICENSE).
  Development graph checks use 1.17.0; release diagnostic fixtures use 1.19.1.
  Python and ONNX Python bindings are not installed for users.
- cryptography 50.0.2: Apache-2.0 OR BSD-3-Clause.
  [Licence](https://github.com/pyca/cryptography/blob/50.0.2/LICENSE).
  Catalogue-signing and test tooling only.
- FFmpeg: LGPL-2.1-or-later by default; GPL-enabled builds have additional requirements.
  [Licensing](https://ffmpeg.org/legal.html). Linux SDK build dependency, not bundled.

## Documentation footage

Some keying examples contain Mixkit footage under the Stock Video Free License.
Each affected image has a licence sidecar and a linked provenance record.
The [licence text](LICENSES/LicenseRef-Mixkit-Stock-Video-Free.txt) applies to that
footage, including processed examples. It does not become GPL-licensed here.