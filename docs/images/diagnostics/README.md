<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Diagnostics captures

29 October 2026, real portable OBS 33.0.0 cloned from the installed application.
Windows 11 25H2, kernel 10.0.26200, runtime Qt 6.11.1, D3D11, RTX 5070 Ti.
The module uses the OBS 30.2 SDK. The test ran in an isolated CorridorKey Test
collection while another package held the installed-OBS lock.

| File | Contents |
| --- | --- |
| [obs33-preview.png](obs33-preview.png) | Production diagnostics preview before copying; machine/GPU fields. |
| [obs33-timing.png](obs33-timing.png) | The same report scrolled to W15 timing and chosen transport. |
| [obs33-report.txt](obs33-report.txt) | The exact reviewed clipboard report from that run. |

Both PNGs are 1160 × 899 pixels, captured with `QWidget::grab()` from the real
dialog. No mouse cursor or native title bar is included. The test sets
`QT_SCALE_FACTOR=0.75` on this machine's 200% desktop. No pixels or displayed
values were edited.

The scene is a private 1280 × 720 color card; the canvas is 1920 × 1080 at 30 Hz.
W03's diagnostic fake uses a 20 ms configured delay and staged-grid transport.
The timing fields are actual W15 observations of that fake path, not neural
inference performance. No model or inference runtime bundle is installed in
the capture profile, so those fields report absence.

The fixture inserts private scene/source/audio-name markers, checks that none
appear in the report, presses Copy to clipboard and verifies exact equality
with the preview. It restores the prior clipboard text and removes its private
scene. The report and screenshots were also inspected for personal paths and
credentials. The capture host shut down normally with zero reported leaks.

Reproduce with [run-diagnostics-capture.ps1](../../../tests/ui/run-diagnostics-capture.ps1)
after building with `CORRIDORKEY_MANAGER_FIXTURES=ON` and
`CORRIDORKEY_ENABLE_FAKE_FILTER=ON`. The script uses a dedicated portable host,
never closes a process it did not start and removes its test plugin afterward.
See [W17 validation](../../dev/validation-w17.md) for build settings and limits.
