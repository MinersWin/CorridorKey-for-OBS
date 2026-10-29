<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CK1 (experimental)

For a weak GPU, try CK2 Facecam with Edge refiner off first. CK1's edge quality
is clearly below CK2, including that fallback. CK1 is available under Advanced
as "CorridorKey 1.0 (Experimental)".

## Set up CK1

Open Manage models and install CorridorKey 1.0: Green (Experimental), or the
Blue package for a blue screen. Install MODNet hint as well. Review and accept
each package's model licences before downloading. The plugin remains free;
the models have their own terms.

Turn on Advanced, select CK1, and choose your screen colour. Start at 384x384
with MODNet. A lower grid saves work but loses edges. A higher grid costs more
and still does not match CK2's quality. The input frame, including any manual
crop, is squashed to a square for inference and reconstructed at the original
source size. CK1 keeps separate saved settings from CK2.

Learned edge refinement stays off: the available refiner was evaluated for
CK2. RGB guided reconstruction, foreground recovery and despill still run at
native output size. Blue selects a separate checkpoint and removes blue spill.

## Choose a hint

MODNet is the default. It runs on the same captured image and at the same grid
as CK1. Its person matte can exclude a chair, microphone, prop or another person.
Use Hint view to check what it supplies, then inspect Alpha and the keyed output
on moving footage. Check transparent materials as well as the person's outline.

CK2 Stage 1 needs the CK2 Balanced package. It adds MoGe, RVM and Stage 1 at
512x512 on the same capture, then resizes that alpha to the CK1 grid. The last
hint time appears in the properties; total inference time includes it. Running
this hint can cost more than using CK2 directly. Its temporal state belongs to
this filter and resets after a source or crop change, a cut or a long gap.

Chroma is an Expert option. It estimates a hint from screen-colour excess and
can fail on teal objects or unevenly lit screens. It can help keep a prop that
a person model excludes, but it produces worse edges in the evaluated clips.
Adjust Screen match and Hint softness while watching Hint view. These controls
change the hint, not the model's final edge sharpness. Every hint belongs to the
current captured frame; the plugin never reuses an older hint.

## Quality and cost

The export evaluation on real clips reported these edge errors. Lower is better.
All values use the release evaluation's edge-error metric.

| Pipeline | Edge error |
| --- | ---: |
| CK1 256 + MODNet | 154 |
| CK1 384 + MODNet | 111 |
| CK1 512 + MODNet | 85 |
| CK2 Facecam, refiner off | 61 |
| CK2 Facecam | 34.8 |
| CK2 Balanced | 29.2 |

Export-side timings on an RTX 5070 Ti measured CK1 alone at 4.7 / 5.4 / 7.1 ms
with TensorRT FP16, or 8.9 / 15.8 / 28.5 ms with DirectML FP32, for 256 / 384 /
512. MODNet adds about 1.6 to 2.1 ms on TensorRT or 1.9 to 3.2 ms on DirectML.
These graph measurements exclude OBS capture, reconstruction and presentation.
They are comparison figures from the release brief, not complete plugin frame
times. Native checks and their test conditions are recorded in the
[pipeline validation notes](../dev/ck1-pipeline.md).

If a hint package is missing, a backend cannot run a graph, or a result becomes
too old, the filter shows passthrough with a status message. Install the required
package or select a working backend before trying again. External masks,
MediaPipe and distilled hints have no evaluated implementation in this release.
