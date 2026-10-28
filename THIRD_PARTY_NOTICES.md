<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party notices

This repository contains plugin code and build scaffolding, not model weights
or model graphs. Dependencies retain their own licences. Binary distributors
must preserve the notices shipped by any dependencies they redistribute and
meet their source and relinking obligations.

| Component | Licence | Use / source |
| --- | --- | --- |
| OBS Studio / libobs / frontend API | GPL-2.0-or-later | Host and plugin API; [OBS Project](https://github.com/obsproject/obs-studio) |
| OBS plugin template | GPL-2.0-or-later, used under GPL-3.0-or-later | CMake and formatting scaffolding; [upstream](https://github.com/obsproject/obs-plugintemplate/tree/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc) |
| Qt 6 Core and Widgets | LGPL-3.0 / GPL, or commercial licence | Dynamically linked UI framework supplied by OBS or the distribution; [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) |
| ONNX Runtime | MIT | Optional inference runtime, currently disabled; [Microsoft](https://github.com/microsoft/onnxruntime/blob/v1.31.1/LICENSE) |
| FFmpeg 8.0 | LGPL-2.1-or-later in the minimal SDK configuration | Linux build-only dependency of OBS; not bundled in plugin packages; [FFmpeg licensing](https://ffmpeg.org/legal.html) |

The default packages do not bundle Qt, OBS or ONNX Runtime. Optional future
runtime packages must carry ONNX Runtime's LICENSE and ThirdPartyNotices.txt,
including notices for bundled dependencies. DirectML and vendor GPU libraries
have separate terms; the additional permission does not grant redistribution
rights to those libraries.

## Separately downloaded models

**CorridorKey models are NOT part of this repository or its release packages.**
The planned model manager will download them only at the user's request and
after showing the applicable licence and recording explicit acceptance.
They are not GPL-licensed by this project.

- **CorridorKey 2.0 and CorridorKey 1.0**: credit **Corridor Digital LLC /
  Niko Pueringer**. Subject to the **Corridor Key Licence**, including the
  upstream CC BY-NC-SA 4.0 terms and Corridor Key Additional Terms where
  applicable. The actual revision's licence must be shown before download.
  Sources: [CorridorKey 2.0](https://huggingface.co/nikopueringer/CorridorKey2.0)
  and [CorridorKey 1.0](https://github.com/nikopueringer/CorridorKey).
  CK-derived weight-free graphs also remain outside this repository under
  those separate terms. No upstream CorridorKey implementation code is copied.
- **MoGe-2**: MIT; credit the MoGe authors and Microsoft.
  [Source and licence](https://github.com/microsoft/MoGe). The planned ViT-B
  checkpoint is `Ruicheng/moge-2-vitb-normal` at revision
  `ca5f0e07ff01d3e5a364c1d954ed12ee1814b368`.
- **RVM (Recurrent Video MAE)**: CC BY 4.0; credit Google DeepMind and the
  Recurrent Video MAE authors. This is not Robust Video Matting.
  [Source and attribution](https://github.com/google-deepmind/representations4d).
  Planned checkpoint: `pretrain_rvm_small16_256_204031069.npz`.
- **CorridorKey Realtime Edge Refiner**: planned optional download from
  [mantelmoritz/CorridorKey-Realtime-EdgeRefiner](https://huggingface.co/mantelmoritz/CorridorKey-Realtime-EdgeRefiner).
  Its checkpoint licence and attribution must be verified and included in the
  model manifest before enabling downloads. No checkpoint is included here.

Attribution here is informational and grants no model rights. Verify each
model's exact revision, licence and required notices before adding it to a
download manifest. Model terms do not change this plugin's GPL licence.
