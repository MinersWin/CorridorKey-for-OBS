<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# First-run captures

Captured on 29 October 2026 in isolated portable OBS 33.0.0, Qt 6.11.1,
with the R2 release-audit build compiled against the OBS 30.2 SDK. Each PNG is a real Qt
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

Portable cache paths are shortened in the application to `<Portable OBS>`;
full locations remain available in tooltips and accessible descriptions. Captures
assert that visible labels contain no development cache paths. State captures
check the ready/error/accent colours and task-button visibility in both themes.

Every theme/scale combination includes setup and licence acceptance. The guide uses the labelled download, preparation and Ready examples alongside these production views.

Run `tests/ui/run-obs-captures.ps1` with an isolated portable root and staged
probe tools. Add `-Diagnostics` for the diagnostics set. The runner restores the
test plugin and preferences after each run. See [the release audit](../../dev/release-audit.md)
for checks and remaining release gates.
