<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Properties captures

These are real OBS filter-dialog captures from 29 October 2026. The orange source
is the dedicated CorridorKey Test colour source. The build passes video through;
none of these images shows neural keying or measured model performance.

| Capture | Host and appearance | View |
| --- | --- | --- |
| [Standard](obs33-yami.png) | Installed OBS 33.0.0, Yami / Default, 100% | Default controls and missing-model status |
| [Advanced](obs33-yami-advanced.png) | Installed OBS 33.0.0, Yami / Default, 100% | Separate Advanced and Expert toggles, quality group |
| [Crop](obs33-yami-crop.png) | Installed OBS 33.0.0, Yami / Default, 100% | Expanded native group with four percentage cuts |
| [Light](obs33-yami-light.png) | Installed OBS 33.0.0, Yami / Light, 100% | Default controls in the light palette |
| [Advanced, light](obs33-yami-light-advanced.png) | Installed OBS 33.0.0, Yami / Light, 100% | Quality controls and status |
| [150%](obs30-yami-150.png) | Portable OBS 30.2.0, Yami, English | Labels, tooltips, sliders and scrolling at `QT_SCALE_FACTOR=1.5` |
| [German, 200%](obs30-yami-de-200.png) | Portable OBS 30.2.0, Yami, German | Translated labels at `QT_SCALE_FACTOR=2` |

The installed-OBS test held `installed-obs.lock` as `w02-settings`. Its backed-up
module, locales and preferences were restored before releasing that lock. Another
package then acquired it. The shared portable runtime was also in use, so its
application files were copied to the W02 build directory for the scaling checks,
with an isolated portable configuration. No other OBS process was closed.

The review caught a status row whose long description widened the form's label
column. The final form puts status text in the value column with an empty label
and a tooltip. Standard and Advanced labels remain readable in both palettes.
Percentage cuts and numeric boxes align within their groups. At 150% and 200%,
the host's scroll area keeps controls accessible when the panel exceeds the window.
The source preview was reduced with OBS's own splitter for the captures.

Windows 11, MSVC 19.44 and the OBS 30.2 SDK were used. C++ compilation treats
warnings as errors. The core suite also builds with the plugin disabled, without
an OBS SDK. CTest passed with both OBS 30.2 and 33 runtimes for JSON persistence,
preset/model callbacks, expansion, availability, localization and migration.
Linux, system high-contrast themes and a full keyboard/screen-reader audit were
not tested here. GPU inference, model download and audio-offset execution belong
to later packages.

The final core run passed 24 cases and 2,050 assertions. Both libobs-runtime CTest
runs passed. clang-format, gersemi and Git whitespace checks passed.
