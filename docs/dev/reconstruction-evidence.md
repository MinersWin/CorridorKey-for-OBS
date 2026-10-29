<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Reconstruction validation evidence

29 October 2026. Runtime commit `3e824d6`, rebased onto W15 commit `172e667`.
Windows 11, Ryzen 9 5950X, RTX 5070 Ti. Numeric probes used
OBS 33 D3D11 and OpenGL. Live source screenshots used portable OBS 30.2.
The installed OBS and its plugin were not modified. The shared desktop was not
isolated from other applications.

The capture baseline precedes W08. The latency baseline includes reconstruction,
five slots and the merged W15 hooks, before the asynchronous handoff change.
The final runs use the balanced 512 x 512 grid, a 60 Hz canvas and the fake pipeline.
Counters are cumulative. Each W15 timing snapshot covers the preceding five seconds.

## Throughput and age

### baseline-1080.log

```text
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1201 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.06 captures=602 submitted=602 completed=601 dropped=597 repeated=0 status=0 graphics_tasks=1200 graphics_ms_p50<=0.30 graphics_ms_p95<=0.47
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1802 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.06 captures=903 submitted=902 completed=902 dropped=897 repeated=0 status=0 graphics_tasks=1801 graphics_ms_p50<=0.30 graphics_ms_p95<=0.48
```

### baseline-4k.log

```text
info: [corridorkey:w03] instance=1 native=3840x2160 callbacks=1201 self_ms_p50<=0.04 self_ms_p95<=0.06 total_ms_p50<=0.05 total_ms_p95<=0.08 captures=596 submitted=596 completed=596 dropped=603 repeated=0 status=0 graphics_tasks=1182 graphics_ms_p50<=0.31 graphics_ms_p95<=0.75
info: [corridorkey:w03] instance=1 native=3840x2160 callbacks=1802 self_ms_p50<=0.04 self_ms_p95<=0.07 total_ms_p50<=0.05 total_ms_p95<=0.09 captures=883 submitted=883 completed=882 dropped=917 repeated=0 status=0 graphics_tasks=1717 graphics_ms_p50<=0.34 graphics_ms_p95<=0.87
```

### latency-before-0.log

```text
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=595 self_ms_p50<=0.04 self_ms_p95<=0.05 total_ms_p50<=0.06 total_ms_p95<=0.07 captures=594 submitted=593 completed=592 dropped=0 repeated=0 status=0 graphics_tasks=594 graphics_ms_p50<=1.18 graphics_ms_p95<=2.01
info: [corridorkey:w08] age_median_ms=50.050 age_p95_ms=50.066 input_ms=19.153 output_ms=6.722 inference_ms=1.738 queue_ms=0.001 keyed_fps=60.000
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1195 self_ms_p50<=0.04 self_ms_p95<=0.06 total_ms_p50<=0.06 total_ms_p95<=0.09 captures=1184 submitted=1179 completed=1178 dropped=14 repeated=0 status=0 graphics_tasks=1184 graphics_ms_p50<=1.24 graphics_ms_p95<=2.28
info: [corridorkey:w08] age_median_ms=50.061 age_p95_ms=66.746 input_ms=19.644 output_ms=6.277 inference_ms=1.734 queue_ms=0.001 keyed_fps=57.191
```

### latency-before-20.log

```text
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=600 self_ms_p50<=0.04 self_ms_p95<=0.06 total_ms_p50<=0.06 total_ms_p95<=0.08 captures=599 submitted=200 completed=199 dropped=398 repeated=0 status=0 graphics_tasks=599 graphics_ms_p50<=0.15 graphics_ms_p95<=1.34
info: [corridorkey:w08] age_median_ms=100.055 age_p95_ms=116.735 input_ms=19.145 output_ms=8.270 inference_ms=33.691 queue_ms=0.134 keyed_fps=19.866
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1200 self_ms_p50<=0.04 self_ms_p95<=0.06 total_ms_p50<=0.06 total_ms_p95<=0.08 captures=1199 submitted=400 completed=399 dropped=798 repeated=0 status=0 graphics_tasks=1199 graphics_ms_p50<=0.15 graphics_ms_p95<=1.33
info: [corridorkey:w08] age_median_ms=100.054 age_p95_ms=116.729 input_ms=19.127 output_ms=8.460 inference_ms=33.695 queue_ms=0.128 keyed_fps=19.866
```

### newest-1080-0.log

```text
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=600 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.07 captures=599 submitted=598 completed=597 dropped=0 repeated=0 status=0 graphics_tasks=599 graphics_ms_p50<=0.58 graphics_ms_p95<=1.12
info: [corridorkey:w08] age_median_ms=33.370 age_p95_ms=33.662 input_ms=18.107 output_ms=7.718 inference_ms=1.603 queue_ms=0.001 keyed_fps=60.000
info: [corridorkey:w08] post_cpu_ms_p50<=0.48 post_cpu_ms_p95<=0.66
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.16 tick_transfer_cpu_ms_p95<=0.30
info: [corridorkey:w08] fake_cpu_ms_p50<=5.33 fake_cpu_ms_p95<=6.80 worker_cpu_ms_p50<=5.40 worker_cpu_ms_p95<=9.70
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1200 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.07 captures=1199 submitted=1196 completed=1195 dropped=2 repeated=0 status=0 graphics_tasks=1197 graphics_ms_p50<=0.65 graphics_ms_p95<=1.32
info: [corridorkey:w08] age_median_ms=33.383 age_p95_ms=34.096 input_ms=18.137 output_ms=7.899 inference_ms=1.661 queue_ms=0.001 keyed_fps=59.397
info: [corridorkey:w08] post_cpu_ms_p50<=0.53 post_cpu_ms_p95<=0.91
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.16 tick_transfer_cpu_ms_p95<=0.42
info: [corridorkey:w08] fake_cpu_ms_p50<=5.74 fake_cpu_ms_p95<=7.20 worker_cpu_ms_p50<=5.87 worker_cpu_ms_p95<=10.54
```

### newest-4k-0.log

```text
info: [corridorkey:w03] instance=1 native=3840x2160 callbacks=600 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.07 captures=599 submitted=598 completed=597 dropped=0 repeated=0 status=0 graphics_tasks=600 graphics_ms_p50<=0.77 graphics_ms_p95<=1.12
info: [corridorkey:w08] age_median_ms=33.381 age_p95_ms=33.623 input_ms=18.176 output_ms=7.953 inference_ms=1.927 queue_ms=0.000 keyed_fps=59.996
info: [corridorkey:w08] post_cpu_ms_p50<=0.49 post_cpu_ms_p95<=0.68
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.17 tick_transfer_cpu_ms_p95<=0.32
info: [corridorkey:w08] fake_cpu_ms_p50<=4.88 fake_cpu_ms_p95<=6.23 worker_cpu_ms_p50<=5.05 worker_cpu_ms_p95<=8.39
info: [corridorkey:w03] instance=1 native=3840x2160 callbacks=1200 self_ms_p50<=0.03 self_ms_p95<=0.05 total_ms_p50<=0.04 total_ms_p95<=0.07 captures=1199 submitted=1198 completed=1198 dropped=0 repeated=0 status=0 graphics_tasks=1200 graphics_ms_p50<=0.61 graphics_ms_p95<=1.09
info: [corridorkey:w08] age_median_ms=33.361 age_p95_ms=33.623 input_ms=17.878 output_ms=8.447 inference_ms=1.658 queue_ms=0.000 keyed_fps=60.001
info: [corridorkey:w08] post_cpu_ms_p50<=0.49 post_cpu_ms_p95<=0.83
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.18 tick_transfer_cpu_ms_p95<=0.38
info: [corridorkey:w08] fake_cpu_ms_p50<=5.13 fake_cpu_ms_p95<=6.38 worker_cpu_ms_p50<=4.93 worker_cpu_ms_p95<=8.41
```

### newest-1080-20.log

```text
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=600 self_ms_p50<=0.04 self_ms_p95<=0.06 total_ms_p50<=0.06 total_ms_p95<=0.09 captures=598 submitted=357 completed=356 dropped=240 repeated=0 status=0 graphics_tasks=599 graphics_ms_p50<=0.41 graphics_ms_p95<=0.90
info: [corridorkey:w08] age_median_ms=66.709 age_p95_ms=83.592 input_ms=18.686 output_ms=11.515 inference_ms=20.787 queue_ms=8.500 keyed_fps=35.114
info: [corridorkey:w08] post_cpu_ms_p50<=0.60 post_cpu_ms_p95<=0.94
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.21 tick_transfer_cpu_ms_p95<=0.45
info: [corridorkey:w08] fake_cpu_ms_p50<=5.04 fake_cpu_ms_p95<=6.33 worker_cpu_ms_p50<=1.71 worker_cpu_ms_p95<=8.41
info: [corridorkey:w03] instance=1 native=1920x1080 callbacks=1200 self_ms_p50<=0.04 self_ms_p95<=0.07 total_ms_p50<=0.06 total_ms_p95<=0.11 captures=1198 submitted=702 completed=701 dropped=495 repeated=0 status=0 graphics_tasks=1199 graphics_ms_p50<=0.41 graphics_ms_p95<=0.95
info: [corridorkey:w08] age_median_ms=66.892 age_p95_ms=83.443 input_ms=18.994 output_ms=12.427 inference_ms=20.751 queue_ms=0.443 keyed_fps=33.507
info: [corridorkey:w08] post_cpu_ms_p50<=0.65 post_cpu_ms_p95<=1.03
info: [corridorkey:w08] tick_transfer_cpu_ms_p50<=0.22 tick_transfer_cpu_ms_p95<=0.52
info: [corridorkey:w08] fake_cpu_ms_p50<=5.42 fake_cpu_ms_p95<=6.99 worker_cpu_ms_p50<=1.80 worker_cpu_ms_p95<=9.50
```

## Numeric checks

Each renderer runs twelve fixtures and eleven comparisons per fixture. RGBA16F
storage has its own `5e-4` tolerance; all other fields use the FP32 tolerances in
[the reconstruction contract](reconstruction.md). Full logs remain in the local
build directory. They can be regenerated with `corridorkey_post_probe`.

| Field | D3D11 maximum absolute error | OpenGL maximum absolute error |
| --- | ---: | ---: |
| coefficients | 3.86233187e-06 | 3.64785337e-06 |
| delta | 5.46327461e-08 | 5.46327461e-08 |
| background | 2.17758519e-07 | 2.17758519e-07 |
| band | 1.25251087e-07 | 1.25251087e-07 |
| reduction | 1.36730499e-06 | 1.36730499e-06 |
| raw | 2.93381259e-06 | 3.14473981e-06 |
| finished | 4.18360466e-07 | 5.1864842e-07 |
| refined-seam | 4.18360466e-07 | 5.1864842e-07 |
| half-storage | 0.000483911186 | 0.000483911186 |
| srgb-composite | 3.23175541e-07 | 5.79454914e-07 |
| linear-composite | 4.18360466e-07 | 5.1864842e-07 |

## Lifecycle and build checks

The offscreen harness exercises 24 changes across odd portrait dimensions,
crop, enable/disable, delayed and reordered fake results, and invalid output.
It then detaches and destroys the filter while work is pending. The process
returns normally after the OBS destroy queue and UI queue drain.

The HDR probe returns an extended-709 source space through the filter and reports:

```text
HDR status: Passthrough ? CK-E015<br>HDR input is not supported by this model; passthrough.
```

Both default and diagnostic MSVC builds use the OBS 30.2 SDK and warnings as errors. Clang-format 19 and
gersemi 0.22.2 checks pass. Changed translation units compile to Linux x86-64
objects with Zig 0.15.2 Clang and `-Wall -Wextra -Werror`. Eleven CTest groups pass,
including local weight-binder parity. That parity run required Python 3.12:
the initially selected Python 3.9 lacks `hashlib.file_digest`.

This evidence does not cover a real neural model, learned refinement, Linux
runtime/fence behaviour, AMD/Intel hardware or HDR reconstruction.
