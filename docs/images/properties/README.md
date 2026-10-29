<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Filter properties captures

Captured on 29 October 2026 in isolated portable OBS 33.0.0, Qt 6.11.1,
with the D1 build compiled against the OBS 30.2 SDK. Each PNG is a real Qt
widget capture, with no cursor or desktop overlays. The test profile disables
obs-websocket and contains no browser sources. Installed OBS was left untouched.

| Appearance | 100% | 150% |
| --- | --- | --- |
| Yami | [View](obs33-yami.png) | [View](obs33-yami-1.5.png) |
| Yami Light | [View](obs33-yami-light.png) | [View](obs33-yami-light-1.5.png) |

Metadata records the actual device pixel ratio: 1.0 and 1.5, respectively.
On the capture machine the native desktop scale was 200%, so the runner used
`-HostScale 2` to compensate. No image was rescaled after capture.

Each combination has standard, `-crop` and `-advanced` views. The test source
has no installed model. QWidget capture excludes the GPU-rendered preview;
these images document controls rather than key quality. English and German
property callbacks passed the libobs test, but this capture set is English.

Run `tests/ui/run-obs-captures.ps1` with an isolated portable root and staged
probe tools. Add `-Diagnostics` for the diagnostics set. The runner restores the
test plugin and preferences after each run. See [the design review](../../dev/design-review.md)
for the comparison, checks and remaining work.
