<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W09 screenshot sources

The W09 PNGs are source screenshots from the isolated OBS 33 test instance,
saved through obs-websocket on 2026-10-29. They contain actual CK2 output with
W08 native reconstruction and no learned edge refiner. OBS saved the renders
directly without a cursor. Transparent areas may appear black in viewers.

- `w09-facecam-dml-1080.png`: 1920x1080 reporter footage, DirectML FP32.
- `w09-facecam-dml-4k.png`: 3840x2160 headphones/dance footage, DirectML FP32.
- `w09-facecam-trt-1080.png`: 1920x1080 reporter footage after a source-size
  reset, with the same TensorRT precision/capture policy as the 4K test.
- `w09-facecam-trt-4k.png`: 3840x2160 headphones/dance footage, TensorRT with
  captured RVM FP32 and Stage 1 FP16; uncaptured MoGe FP32.

The source clips are Mixkit's [female reporter](https://mixkit.co/free-stock-video/female-reporter-reporting-with-microphone-in-hand-on-a-chroma-28293/)
and [girl dancing with earphones](https://mixkit.co/free-stock-video/girl-dancing-with-her-earphones-on-a-green-background-28306/).
Both asset pages identify the Mixkit Stock Video Free License. The footage
retains its original terms; the plugin's GPL licence does not relicense it.
See [Mixkit's licence page](https://mixkit.co/license/).

Measurements, presets and limitations are in [the pipeline report](../../dev/ck2-pipeline.md).
