<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Model manager captures

Captured on 29 October 2026 in isolated portable OBS 33.0.0, Qt 6.11.1,
with the D1 build compiled against the OBS 30.2 SDK. Each PNG is a real Qt
widget capture, with no cursor or desktop overlays. The test profile disables
obs-websocket and contains no browser sources. Installed OBS was left untouched.

| Appearance | 100% | 150% |
| --- | --- | --- |
| Yami | [View](yami-first-run.png) | [View](yami-1.5-first-run.png) |
| Yami Light | [View](yami-light-first-run.png) | [View](yami-light-1.5-first-run.png) |

Metadata records the actual device pixel ratio: 1.0 and 1.5, respectively.
On the capture machine the native desktop scale was 200%, so the runner used
`-HostScale 2` to compensate. No image was rescaled after capture.

The first-run manager and setup licence views use the bundled production
catalogue. Progress, failure, update and Ready views use the explicitly labelled
tiny synthetic fixture. The import-ready view follows a real accepted folder
import and hash verification. None of these states measures neural inference.

Every theme/scale combination includes setup and licence acceptance. The manager set also covers downloading, verifying, binding, building, testing, Ready, update, error, diagnostics, unavailable backend and the compact window.

Run `tests/ui/run-obs-captures.ps1` with an isolated portable root and staged
probe tools. Add `-Diagnostics` for the diagnostics set. The runner restores the
test plugin and preferences after each run. See [the design review](../../dev/design-review.md)
for the comparison, checks and remaining work.
