<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Diagnostics captures

Captured on 29 October 2026 in isolated portable OBS 33.0.0, Qt 6.11.1,
with the D1 build compiled against the OBS 30.2 SDK. Each PNG is a real Qt
widget capture, with no cursor or desktop overlays. The test profile disables
obs-websocket and contains no browser sources. Installed OBS was left untouched.

| Appearance | 100% | 150% |
| --- | --- | --- |
| Yami | [View](yami-preview.png) | [View](yami-1.5-preview.png) |
| Yami Light | [View](yami-light-preview.png) | [View](yami-light-1.5-preview.png) |

Metadata records the actual device pixel ratio: 1.0 and 1.5, respectively.
On the capture machine the native desktop scale was 200%, so the runner used
`-HostScale 2` to compensate. No image was rescaled after capture.

Each combination has the report preview, its timing section and a redacted
text report. Clipboard equality and private-marker checks passed. The test
uses a synthetic colour source without a neural model; missing timings remain
null. Zero OBS memory leaks were reported on exit.

Run `tests/ui/run-obs-captures.ps1` with an isolated portable root and staged
probe tools. Add `-Diagnostics` for the diagnostics set. The runner restores the
test plugin and preferences after each run. See [the design review](../../dev/design-review.md)
for the comparison, checks and remaining work.
