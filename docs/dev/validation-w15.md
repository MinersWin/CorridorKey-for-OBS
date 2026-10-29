<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W15 delay measurement and audio sync

29 October 2026. Tests use the deterministic fake pipeline and staged input
capture. No neural model or real microphone was used.

## Implementation and integration seams

`telemetry/obs_clock.hpp` wraps `os_gettime_ns()` in the existing `TimePoint` type.
The OBS capture worker, submissions, fake completion, presentation and transfer
measurements use this clock. The video timestamp remains a draw-deduplication key.
Portable tests supply their own times. Do not mix `Clock::now()` and `obs_now()` in
one pipeline; their epochs are unrelated.

`FrameTiming` records successful presentations once per OBS video tick in a fixed
1024-entry ring. Repeating a sequence keeps its original capture time and adds a
new age sample. Multiple views in the same tick add one sample. Snapshots use
up to five seconds or 1024 entries, whichever is shorter. They use nearest-rank
median/p95 and count inference
and transfer phases once per newly displayed result. The render-side recorder
uses a try-lock and skips a sample if snapshot copying holds the lock. Sorting
and allocation happen in the control-side snapshot. A reset clears the window.

Input transfer is the wall interval from capture through staged readiness and
CPU analysis conversion. It includes the capture fence and graphics task wait.
Queue time is the remaining interval before submission. Output transfer covers
CPU packing, waiting for the graphics upload task, and enqueueing that upload.
These are host intervals, not GPU event timings. W08 and production transports
should replace these measurements with their own phase boundaries while keeping
the original `FrameIdentity::captured_at`. No blocking GPU timing query was added.

`StatusSnapshot` exposes age median/p95, inference median, separate input/output
transfer medians, queue median and processed FPS. Repeated presentations are a
five-second count; submission, completion and dropped counters retain W03's
lifetime scope. These fields are available to W17 without reading Qt labels.
Timings become unavailable after 250 ms without a successful presentation.

The properties form owns a 500 ms Qt timer and weak filter reference. It finds
its three information labels through invisible per-form HTML markers, then
updates those labels directly. It never calls `obs_source_update_properties()`.
Mouse interaction, open popups and the 750 ms after an edit defer refresh.
QPointer references tolerate label replacement during normal property edits.
Non-Qt builds still provide snapshots and refresh when properties are opened.

The portable controller runs on the OBS UI/control queue through `AudioSyncRuntime`.
Its process-wide registry reserves a microphone by UUID. It captures a baseline,
waits for two seconds of valid samples, and computes baseline + median age + trim.
The complete added correction is capped by `sync_limit_ms`. Writes are at least
two seconds apart, require a 5 ms difference, and later writes slew at 10 ms/s.
The first correction applies in full. Source private settings retain the baseline
and last owned value so a saved active offset is not added again after reopening.
Recovery uses that baseline only when the saved last value still matches.

Every restore compares the current offset with the last owned value. A manual
edit latches manual mode, including for a second filter already waiting for that
microphone. A per-filter private setting retains the manual latch when the scene is saved.
Explicitly toggling auto sync or choosing another source rearms it.
Disable, bypass, failure, missing video and teardown release the reservation;
source switches restore the previous microphone first. Rename is display-only.

Shared-file edits are limited to the status fields, a weak-source accessor on
`FilterControl`, filter lifecycle/settings wiring, and capture timing hooks in
`filter_input.cpp`. `staged_transport.cpp` only switches its existing host-draw
clock calls to `obs_now()`. No reconstruction, shader, transport ownership or
capture-admission behavior changed. W08 should retain these clock and timing
hooks when merging its transport changes. The new build fragment is `w15.cmake`.

## Deterministic delay accuracy

The test submits an input eight milliseconds after capture, advances the actual
fake pipeline by its configured delay, then records presentation at completion
plus 2 ms and four repeats at 10 ms intervals. The baseline is 17 ms and trim is
3 ms. Each row passes exact comparisons; there is no histogram rounding.

| Fake delay | Age median | Age p95 | Inference | Applied total offset |
| ---: | ---: | ---: | ---: | ---: |
| 0 ms | 30 ms | 50 ms | 0 ms | 50 ms |
| 20 ms | 50 ms | 70 ms | 20 ms | 70 ms |
| 80 ms | 110 ms | 130 ms | 80 ms | 130 ms |
| 160 ms | 190 ms | 210 ms | 160 ms | 210 ms |

The age error is zero against this scheduled fixture, and offset error is zero
against baseline + median + trim. This validates timestamp propagation and the
controller's arithmetic; it does not measure a camera or a neural backend.

Unit tests also cover opt-in and explicit pairing, warmup, rate and hysteresis,
slew, caps and negative trim, manual edits during warmup or active control,
shared-source reservations, UUID changes, deletion, failure, teardown and saved
baseline recovery. Old windows expire, generations reset, and duplicate views do
not inflate presentation counts.

## Build and automated checks

Both the diagnostic and default plugins compile against OBS SDK 30.2 with MSVC
19.44, C++20 and compiler warnings treated as errors. CTest passes all six test
suites. The portable contract executable reports 41 cases and 2,198 assertions.
MSBuild still emits its existing module-dependency warning for the two files
named `cache.cpp`; no C++ compiler warning was accepted.

Zig 0.15.2's Clang compiles the telemetry, controller, OBS adapter, both filter
modes, properties, filter input and staged transport to Linux x86-64 objects with
`-Wall -Wextra -Werror`. This is object compilation, not Linux linking or a GCC
runtime test. clang-format 19.1.5 and gersemi 0.22.2 checks pass, including all
tracked CMake files. Git whitespace checks pass.

## OBS evidence

Windows 11, Ryzen 9 5950X, RTX 5070 Ti, D3D11, 3840 x 2160 canvas, 60 Hz. The
filter input was a 1920 x 1080 color card. A muted looping WAV media source had a
17 ms baseline and 3 ms trim. The dedicated profile was `CorridorKey W15`, in the
`CorridorKey Test` collection. No stream, recording or owner's audio source was
changed. Source filters were open for the screenshots, adding a second view.

Portable OBS 30.2.0 was used while another session held the installed-host lock.
After that session exited and released it, W15 acquired `installed-obs.lock` and
tested installed OBS 33.0.0. Both used the baseline OBS 30.2 SDK build.

| Host and fake delay | Processing median | Transfer median sum | Displayed age median / p95 | Actual audio offset |
| --- | ---: | ---: | ---: | ---: |
| OBS 30.2, 0 ms | 1.5 ms | 27.6 ms | 50.1 / 66.7 ms | 70.1 ms |
| OBS 30.2, 40 ms | 51.4 ms | 28.5 ms | 150.3 / 183.4 ms | 170.3 ms |
| OBS 33, 20 ms | 35.5 ms | 28.3 ms | 116.7 / 133.4 ms | 136.7 ms |

Values are individual periodic status samples after warmup, rounded to 0.1 ms.
The fake runs in W03's graphics-paced worker, so its observed wait can exceed its
configured delay. Staging, pending work and repeated output also make displayed
age exceed that delay. Each listed total matches baseline plus median plus trim to the shown precision. This is not an isolated performance benchmark.

The open status label changed after switching fake delay from 40 to 0 ms without
closing the properties window. With 80 ms configured, repeated results sometimes
reached the 250 ms stale limit; auto sync restored the baseline and restarted its
valid-sample window.
On OBS 33 a manual change to 99 ms survived subsequent controller checks and
filter disable. The status explicitly reported the manual override. An injected
OOM restored the 99 ms baseline; clearing the fault resumed correction. Renaming
the source preserved its UUID and control. Reopening a saved active scene kept
the 99 ms baseline and settled at 218.3 ms total. Disabling the filter restored
99 ms. Deleting the dummy audio source stopped control and reported the missing
source. Both installed-host runs shut down normally and reported one allocation
leak, matching the earlier foundation test's count. Its origin is still unknown;
these runs do not establish leak-free shutdown.

The original global settings and test collection files were restored after
shutdown. Both W15 test DLLs were moved out of the OBS plugin search paths, and
both test locks were released. The installed plugin data directory was preserved.

![Actual status, OBS 33 diagnostic build](../images/sync/status.png)

![Actual sync settings, OBS 33 with muted dummy audio](../images/sync/settings.png)

The PNGs are crops of real OBS screenshots with the pointer outside the crop.
No displayed value was edited. See [the user guide](../user/lip-sync.md) for the
clap-test procedure and external-latency limits.

## Remaining qualification

Linux linking/runtime, a physical camera/microphone clap recording, real-model
inference and asynchronous GPU phase measurements still need qualification.
No AMD or Linux hardware claim follows from these Windows fake tests. Production
pipelines must publish the same timing fields when W09/W08 replace the fake path.
