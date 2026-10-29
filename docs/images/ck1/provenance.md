<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CK1 source renders

These images were captured on 29 October 2026 with portable OBS 33.0.0 on
Windows 11 and an RTX 5070 Ti. OBS rendered the source at 1920x1080; its
source-screenshot API returned 1280x720 PNGs. Capture requests sought to 4000 ms
and paused the clip. The filtered views share a paused frame; the unfiltered
context image was captured separately and shows a different frame from the clip.
This API captures the source without desktop chrome or a mouse cursor.
The keyed PNGs retain alpha. These are unedited source captures.

The source is Mixkit's
[female reporter with a microphone](https://mixkit.co/free-stock-video/female-reporter-reporting-with-microphone-in-hand-on-a-chroma-28293/),
under the [Mixkit Stock Video Free License](https://mixkit.co/license/).
Each PNG has a licence sidecar. The video itself is not stored in this repository.

| File | OBS filter settings |
| --- | --- |
| `reporter-source.png` | Filter disabled; separate context frame from the clip |
| `ck1-modnet-256-key-dml.png` | Green CK1, MODNet, 256, DirectML FP32 |
| `ck1-modnet-384-key-dml.png` | Green CK1, MODNet, 384, DirectML FP32 |
| `ck1-modnet-512-key-dml.png` | Green CK1, MODNet, 512, DirectML FP32 |
| `ck1-modnet-384-hint-dml.png` | Raw MODNet hint at 384 |
| `ck1-modnet-384-alpha-dml.png` | Finished CK1 alpha at 384 |
| `ck1-chroma-384-key-dml.png` | Green CK1, Expert chroma hint, 384, DirectML FP32 |
| `ck1-modnet-384-key-trt-interop.png` | Green CK1, MODNet, 384, captured TensorRT FP16, D3D11/CUDA transport |

All keys use RGB guided reconstruction, delta foreground recovery and green
despill. Learned refinement and subject ROI are off. Package activation uses
the production catalogue and accepted receipts, without a development model
override. The [validation report](../../dev/ck1-validation.md) records timing
conditions and limits. These images illustrate one frame; the release's
CK1/CK2 edge-error comparison comes from a separate real-clip evaluation.
