<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Foundation validation, 2026-10-28

## Source and configuration checks

- Both C++ translation units compiled as C++20 with Clang 20.1.2 (Zig 0.15.2),
  `-Wall -Wextra -Werror`, against official OBS 30.0.2 and 33.0.0 headers.
  The legacy properties-button call was replaced with `obs_properties_add_button2`,
  available in both SDKs, to keep current-SDK builds free of deprecation warnings.
- clang-format 19.1.7, gersemi 0.22.2, actionlint 1.7.12 and Git whitespace checks
  passed. Workflow/issue YAML, buildspec/preset JSON and Python syntax were checked.
- All 16 localization keys used by the plugin are present in both locales.
  All external workflow actions are pinned by a full commit SHA. SPDX coverage
  was checked; valid JSON files receive their declaration through REUSE.toml.
- OBS source and ORT source archive SHA-256 values were calculated from official
  downloads. ORT Linux archive digests are recorded from the official release API.

## Windows diagnostic load tests

A diagnostic DLL was linked from the OBS 30.0.2-header objects against the OBS C
interface. It used Clang's Windows GNU target, not the production CMake/MSVC path.
The import library was generated from the installed OBS exports; actual loading
on OBS 30 independently confirmed that the used imports exist at the baseline.
Qt and the frontend API are enabled in CMake but unused by this filter, and were
not linked in this diagnostic build. No diagnostic binaries are committed.

| Host | Test | Result |
| --- | --- | --- |
| Official portable OBS 30.0.2, Windows 11 x64 | Dedicated `CorridorKey Test` collection, color source with CorridorKey filter enabled | Module load, scene/filter initialization and normal shutdown succeeded; OBS reported 0 leaks |
| Installed OBS 33.0.0, Windows 11 x64 | Same baseline DLL, dedicated collection and color source/filter | Module load, scene/filter initialization and normal shutdown succeeded; OBS reported 2 outstanding allocations |

Both logs contain `compiled against API 30.0`, the plugin's successful load line,
the active `CorridorKey` filter and the plugin's unload line. OBS 33 also reported
two outstanding allocations with an empty test scene; their origin has not been
attributed. These checks did not compare rendered pixels or measure performance.

Evidence logs on the validation machine:

- OBS 30: `S:/Projects/obs-test/obs-30.0.2/config/obs-studio/logs/2026-10-28 23-37-45.txt`
- OBS 33: `%APPDATA%/obs-studio/logs/2026-10-28 23-38-31.txt`

The older host was extracted from the official OBS release zip into its isolated
test directory and run in portable mode. Portable OBS ignores ProgramData plugin
paths, so its diagnostic DLL and locales were installed inside that test directory.
The installed OBS test used `C:/ProgramData/obs-studio/plugins/corridorkey`.
Only the instances started for these tests were closed. Original installed-OBS
preferences were restored, and the dedicated test collection was retained.

## Not yet verified

The normal `cmake --preset windows-x64` configure currently fails because no Visual
Studio instance is registered. `vswhere -all` remained empty during polling; the
system CMake installer and the requested Build Tools installation remained pending.
A repository-local CMake 3.31.10 and Ninja 1.11.1.4 were available for configuration
and tooling checks. The diagnostic tests above do not verify the Qt/frontend
linkage, OBS SDK bootstrap, MSVC build, installer, Linux packages or hosted CI.

The workflow definitions are validated, but hosted builds and release packaging
must run after the commits are pushed. No push or release publication was performed.
Optional ORT source builds, CUDA/DirectML providers, model downloads and inference
were not run; the engine and model manager are intentionally absent.
