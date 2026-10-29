<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party notices

This repository contains plugin code and build scripts. Model weights and graphs
are downloaded separately. Dependencies retain their own licences. If you
redistribute their binaries, preserve their notices and meet their source and
relinking obligations.

| Component | Licence | Use / source |
| --- | --- | --- |
| OBS Studio / libobs / frontend API | GPL-2.0-or-later | Host and plugin API; [OBS Project](https://github.com/obsproject/obs-studio) |
| OBS plugin template | GPL-2.0-or-later, used under GPL-3.0-or-later | CMake and formatting scaffolding; [upstream](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc) |
| Qt 6 Core and Widgets | LGPL-3.0 / GPL, or commercial licence | Dynamically linked UI framework supplied by OBS or the distribution; [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) |
| ONNX Runtime | MIT | Optional, dynamically loaded inference runtime; [Microsoft](https://github.com/microsoft/onnxruntime/blob/v1.31.1/LICENSE) |
| nlohmann/json 3.11.3 | MIT | Runtime descriptors and helper protocol; copyright Niels Lohmann; [licence](https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT) |
| OpenSSL 3 | Apache-2.0 | Linux SHA-256 through the distribution's libcrypto; [licence](https://github.com/openssl/openssl/blob/openssl-3.0/LICENSE.txt). Windows uses system BCrypt. |
| doctest 2.4.11 | MIT | Test-only framework, fetched by SHA-256; copyright Viktor Kirilov; [licence](https://github.com/doctest/doctest/blob/v2.4.11/LICENSE.txt). Not linked into the plugin. |
| ONNX 1.17.0 | Apache-2.0 | Development/CI graph fixture generator and checker; [licence](https://github.com/onnx/onnx/blob/v1.17.0/LICENSE). Not shipped with the plugin. Its Python dependencies retain their package notices. |
| FFmpeg | LGPL-2.1-or-later by default; builds with GPL components have additional GPL requirements | Linux SDK dependency: distribution libraries for OBS 30.2, minimal 8.0 build for the optional OBS 33 check; not bundled in plugin packages; [FFmpeg licensing](https://ffmpeg.org/legal.html) |
| nlohmann/json | MIT | Strict model metadata parser; system headers or a SHA-256-pinned 3.11.3 fallback. Copyright Niels Lohmann; [licence](https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT). The licence is included in `data/licenses/nlohmann-json.txt`. |
| zlib | Zlib | Bounded ZIP deflate import. Prefer the system/OBS SDK library; Windows without one builds pinned 1.3.2 statically. Copyright Jean-loup Gailly and Mark Adler; [licence](https://zlib.net/zlib_license.html). Notices for SDK 1.3.1 and fallback 1.3.2 are included under `data/licenses/`. |
| libcurl | curl (MIT-inspired) | Linux anonymous HTTPS transport, from the distribution; [copyright and licence](https://curl.se/docs/copyright.html). No curl binary is bundled. |
| OpenSSL 3 | Apache-2.0 | Linux SHA-256 and P-256 catalogue verification, from the distribution; [licence](https://github.com/openssl/openssl/blob/openssl-3.0/LICENSE.txt). No OpenSSL binary is bundled. Windows uses the OS WinHTTP and CNG APIs. |

The default packages do not bundle Qt, OBS or ONNX Runtime. Optional future
runtime packages must carry ONNX Runtime's LICENSE and ThirdPartyNotices.txt,
including notices for bundled dependencies. DirectML and vendor GPU libraries
have separate terms; the additional permission does not grant redistribution
rights to those libraries.

The development runtime inventory, native library sizes and vendor terms are in
[runtime bundles](docs/dev/runtime-bundles.md). The TensorRT-RTX EP is Apache-2.0;
its bundled NVIDIA runtime DLLs are separately licensed. DirectML.dll also has
separate Microsoft terms. No vendor binaries are checked into this repository.

## Separately downloaded models

**CorridorKey models are NOT part of this repository or its release packages.**
The model service permits payload downloads only after explicit acceptance of
the exact licence texts. The model-manager UI is a separate work package.
They are not GPL-licensed by this project.

- CorridorKey 2.0 and CorridorKey 1.0: credit Corridor Digital LLC /
  Niko Pueringer. Subject to the Corridor Key Licence, including the
  upstream CC BY-NC-SA 4.0 terms and Corridor Key Additional Terms where
  applicable. The actual revision's licence must be shown before download.
  Sources: [CorridorKey 2.0](https://huggingface.co/nikopueringer/CorridorKey2.0)
  and [CorridorKey 1.0](https://github.com/nikopueringer/CorridorKey).
  CK-derived weight-free graphs also remain outside this repository under
  those separate terms. No upstream CorridorKey implementation code is copied.
- MoGe-2: MIT; credit the MoGe authors and Microsoft.
  [Source and licence](https://github.com/microsoft/MoGe). The planned ViT-B
  encoder source is the safetensors format conversion at
  [mantelmoritz/MoGe-2-ViT-B-Encoder-safetensors](https://huggingface.co/mantelmoritz/MoGe-2-ViT-B-Encoder-safetensors/tree/3090937b74a09929079cf6c77ad1edd40a0a9aaf),
  derived from `Ruicheng/moge-2-vitb-normal` at revision
  `ca5f0e07ff01d3e5a364c1d954ed12ee1814b368`. Its bundled DINOv2
  Apache-2.0 notice is retained in the full licence registry.
- RVM (Recurrent Video MAE): CC BY 4.0; credit Google DeepMind and the
  Recurrent Video MAE authors. This is not Robust Video Matting.
  [Source and attribution](https://github.com/google-deepmind/representations4d).
  Planned checkpoint: `pretrain_rvm_small16_256_204031069.npz`.
- CorridorKey Realtime Edge Refiner: planned optional download from
  [mantelmoritz/CorridorKey-Realtime-EdgeRefiner](https://huggingface.co/mantelmoritz/CorridorKey-Realtime-EdgeRefiner).
  Its pinned v12 licence matches the CK2 Corridor Key Licence v2.0 text;
  the registry retains the CorridorKey attribution. No checkpoint is included here.
- MODNet: Apache-2.0 for code, models and demos, excluding the upstream GIF files.
  [Published statement at the reviewed revision](https://github.com/ZHKKKe/MODNet/tree/28165a451e4610c9d77cfdf925a94610bb2810fb#license).
  No exact hint checkpoint or model package is enabled yet.

Full model texts, hashes and attribution are in `data/models/licenses-v1.json`.
The production catalogue remains empty until graph releases and their signatures
are available. CK1's GitHub v1 terms and HF metadata need reconciliation before
admitting a CK1 package.

Attribution here is informational and grants no model rights. Verify each
model's exact revision, licence and required notices before adding it to a
download manifest. Model terms do not change this plugin's GPL licence.

## CK1 and MODNet model packages

CorridorKey 1.0 green and blue checkpoints are by Corridor Digital / Niko Pueringer.
Their HF cards list CC-BY-NC-SA-4.0. The model licence acceptance also includes the
published Corridor Key Licence v1 additional terms, preserved in
`data/models/licenses-v1.json` as `LicenseRef-Corridor-Key-1.0`. These model terms
are separate from the GPL plugin. CK1-derived graphs retain the graph publisher's
Corridor Key Licence.

MODNet is by Zhanghan Ke, Kaican Li, Yurou Zhou, Qiong Yan and Rynson W. H. Lau.
The pinned webcam safetensors conversion is by Moritz Mantel. Its complete
Apache-2.0 LICENSE and NOTICE are included together in the licence registry as
`LicenseRef-MODNet-Apache-2.0`; acceptance covers both texts. Graph exports have
their own Corridor Key Licence record. Models and graphs are downloaded and
bound locally, and are not included in plugin installers. Full revisions and
content hashes are in `data/models/sources-v1.json` and the package manifests.
