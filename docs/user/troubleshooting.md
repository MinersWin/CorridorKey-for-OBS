<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Troubleshooting

Start with the status under Edge refiner in the filter properties. Hover it for
the error code and more detail. Video remains unchanged while a required package
or backend is unavailable.

| Symptom | What to check |
| --- | --- |
| CorridorKey is missing from Effect Filters | Close OBS and check the [installation layout](install.md). OBS 30.2-32 and 33+ use different Windows layouts. Remove duplicate manual copies. |
| Models missing | Open Manage models and install the package for the selected preset. Edge refinement and CK1 hints have separate packages. |
| Download stopped or verification failed | Check free space and your connection, then Retry. Verified files and resumable transfers are kept. Do not edit cache files while OBS is using them. |
| Catalogue cannot be verified | Reinstall the same official package to restore its data files. Report a repeated error with diagnostics; do not bypass verification. |
| Preparing weights after the download ends | Local preparation still needs CPU work and disk space. Keep OBS open or use Cancel task to stop at a safe boundary. |
| Building an engine or testing a backend | First-time TensorRT preparation can take minutes. Its activity indicator does not estimate remaining time. |
| Backend unavailable | Check the runtime installation in Tools, restart OBS after changing runtimes, and use Test backend. An ORT collision with another plugin also needs a restart and compatible libraries. |
| Video repeatedly returns to the original image | Check displayed age. Try Facecam with Edge refiner off, then reduce the scene's GPU load. See [performance](performance.md). |
| Coloured fringe or damaged hair | Check screen lighting, crop and despill. Inspect Alpha and Key on moving footage; reduce aggressive matte contrast or choke. |
| A chair or prop disappears with CK1 | MODNet supplies a person hint. Compare Hint view or use CK2; CK1 remains [experimental](ck1.md). |
| Audio is still out of sync | Follow the [clap test](lip-sync.md) and select the intended microphone. Only one filter can control a shared microphone. A manual sync-offset edit releases automatic control. |
| Memory rises with two cameras | Reduce the number of active filters or their presets and test your full session length. The recorded multi-instance memory trend is unresolved. |

Windows SmartScreen warnings are expected for the unsigned installer. Verify the
release source and checksum as described in [installation](install.md).
CorridorKey opens no listening port. A firewall prompt from OBS may concern
another OBS component; model downloads only need outbound HTTPS.

## Report a reproducible problem

Open **Manage models > Copy diagnostics...**. Review the preview, then choose
**Copy to clipboard**. **Report a problem** opens the
[issue chooser](https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose);
it sends nothing automatically. Paste the report and describe the steps, expected
result and actual result. Include whether the source is a camera, image or media file.

An OBS log can help with loading failures. Review it before attaching it because
OBS logs may contain device names and personal paths. The plugin's diagnostics
omit scene names, media and personal cache paths. Check the
[compatibility guide](compatibility.md) for the tested scope of your setup.
