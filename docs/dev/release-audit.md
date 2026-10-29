<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 0.1.0 release audit

Reviewed on 29 October 2026, starting from `e7f039b`. This audit covers the
release documentation, licensing, model-manager polish and version consistency.
It does not replace installing and testing the signed draft assets.

## Findings and fixes

REUSE lint initially found missing licence files and copyright annotations,
including JSON metadata that its line-based parser could not interpret. The
repository now carries the required licence texts, REUSE annotations and SVG
headers. Generated SPDX strings are excluded from source-header parsing.
Hash-pinned manifests were left unchanged. CI now runs REUSE lint alongside the
format checks. REUSE lint passes for all 609 files, and all 250 reviewed source
files have an SPDX header.

The notices now cover bundled ONNX Runtime and DirectML, compiled nlohmann/json
and zlib, host Qt/OBS, Linux OpenSSL/curl, test-only doctest and optional NVIDIA
downloads. They also distinguish the plugin's GPL licence from model terms and
retain CorridorKey attribution to Corridor Digital LLC / Niko Pueringer.

The model acceptance texts were compared with the pinned publisher material:

| Source | Result |
| --- | --- |
| CK2 | Full Corridor Key Licence v2.0 matches the pinned upstream LICENSE. |
| CK1 green and blue | Both model cards specify CC BY-NC-SA 4.0; the additional acceptance text matches the published v1 LICENSE. |
| MoGe encoder conversion | MIT text and the DINOv2 Apache-2.0 notice match upstream and the conversion repository. |
| RVM | CC BY 4.0 legal text matches; the publisher's README identifies the non-code checkpoint grant. |
| MODNet | Apache-2.0 LICENSE and the conversion NOTICE are preserved. Corrected the attribution to Jiayu Sun. |
| Edge refiner v12 | Licence matches the pinned publisher text, ignoring trailing newlines. |
| ONNX graphs revision 4 | Licence matches the graph repository's text. |

Acceptance texts and their hashes did not change. An obsolete Apache licence
scope description was corrected. Refiner catalogue entries, source revisions and
package manifests are outside this patch; a later catalogue update needs its own
licence/hash review before tagging.

The provenance spot-check compared the C++ pipeline, post-processing, refinement
and shaders with the research implementation and prohibited CK-derived sections.
No copied block monkeypatches, fused depthwise implementation or CK colour-encoding
constants were found. The generic reconstruction code cites the guided-filter
paper. This is a targeted source review, not a claim that string searches prove
independent authorship. No model weights or derived graphs are tracked in Git.

README and the user guides now describe the implemented engine and runtime
manager, distinguish measured support from expected support, and explain the
remaining limitations. User instructions contain no development overrides or
environment-variable setup. A broken guided-filter paper link was replaced with
the author's hosted copy. The old installer-version image is no longer used in
the installation guide. Edited prose received a humanizer pass.

Model-manager state icons use OBS semantic palette colours for ready/error and
the palette accent for active work. Shapes and text still identify each state.
The redundant disabled action button is hidden while a task runs; Cancel remains
available. Portable cache paths display relative to `<Portable OBS>`, while the
tooltip and accessible description retain the full location. Both Windows and
forward-slash path separators are handled.

`buildspec.json` was already `0.1.0`. The installer derives its version from that
file, and both locally built DLL version resources report `0.1.0`. Module
description text now includes the generated version and no longer describes a
pass-through-only foundation.

## Validation

- Windows baseline OBS 30.2 / Qt 6.6.3 and current OBS 33 / Qt 6.11.1 builds pass
  with the full engine enabled and compiler warnings treated as errors.
- Both CTest runs pass their 14 runnable tests. The fifteenth,
  `binder_local_parity`, is skipped because it requires external local fixtures.
- The catalogue-signing suite passes all 18 tests using disposable test keys.
  The production private key was not accessed.
- Packaging inventory tests pass for both Windows layouts, including model and
  secret exclusions. Synthetic ONNX probe-fixture generation passes.
- clang-format 19, gersemi 0.22.2, REUSE lint and `git diff --check` pass.
- All 479 Markdown link/image references resolve locally; all 129 distinct
  linked web URLs respond successfully (GET retried where HEAD was rejected).
- UI captures exercise real OBS 33 widgets with the production catalogue and
  labelled synthetic task states. Tests check task-button visibility, state
  colours and absence of visible development cache paths. See the
  [capture record](../images/model-manager/README.md) for themes and scale factors.

No live-keying performance measurements were repeated for this UI/documentation
patch. Existing native-engine measurements remain linked from the
[compatibility matrix](../compatibility.md) and [performance guide](../user/performance.md).
The installed OBS configuration was not changed.

## Release decision

The changes are suitable for merging toward `v0.1.0-rc.1`. The exact release
commit still needs green hosted Windows/Linux checks, any planned catalogue
update, and the [signed-draft installation checks](release-checklist.md).
No tag, published release or production signature was created during this audit.

Retain these limitations in the candidate notes: Linux GPU interop is absent;
native Linux runtime coverage is limited; AMD/Intel need community validation;
CK1 is experimental; the two-camera memory trend is unresolved; and Windows
binaries are unsigned and can trigger SmartScreen. Flatpak and HDR input remain
unsupported. The memory trend warrants a longer soak before recommending the
candidate for unattended production streams.
