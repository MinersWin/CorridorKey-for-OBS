<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Foundation validation, 28 and 29 October 2026

## Windows builds

The CMake/MSVC build passed against both the OBS 30.2.0 baseline and OBS 33.0.0.
Qt and the frontend API were enabled; optional ONNX Runtime fetching was off.
The toolchain was Visual Studio Build Tools 2022 17.14.42, MSVC 19.44, Windows SDK
10.0.26100.0 and CMake 3.31.10. The baseline archive provides Qt 6.6.3; the
current-SDK archive provides Qt 6.11.1.

Build Tools is installed at `S:/Projects/CorridorKey-for-OBS/.cache/VS2022`, which
is gitignored. Use `vswhere -all -products *` to include Build Tools in discovery.
The default product filter excludes it. CMake and Ninja were also available in
the repository-local `.venv`.

The baseline build used `windows-x64`. The current-SDK check used
`windows-ci-x64 -B build_current -DCORRIDORKEY_OBS_SDK=current`, with compiler
warnings treated as errors. A separate legacy-layout build reused the baseline
SDK through `CORRIDORKEY_USE_BUNDLED_SDK=OFF` and `CMAKE_PREFIX_PATH`.

Both Windows install layouts passed staging checks. Each install removed a
seeded copy of the alternate module before installing its own DLL. The staged
packages contain the module, PDB, locales and licensing notices. The production
DLL imports OBS and Windows/CRT functions; it has no ONNX Runtime or vendor GPU
runtime dependency. Qt and frontend targets are linked by CMake, but the current
filter uses no symbols from them.

## OBS load tests

The MSVC builds ran on Windows 11 x64 with a dedicated `CorridorKey Test`
collection containing a color source and an enabled CorridorKey filter.

| Host | Installed module | Result |
| --- | --- | --- |
| Official portable OBS 30.2.0 | `obs-plugins/64bit/corridorkey.dll` | One module load; active filter; normal shutdown; OBS reported 0 leaks |
| Installed OBS 33.0.0 | `%ProgramData%/obs-studio/plugins/corridorkey/corridorkey.dll` | One module load; active filter; normal shutdown; OBS reported 1 outstanding allocation |

Both logs report `compiled against API 30.2`, a successful plugin load, the active
filter and the plugin's unload. The OBS 33 allocation has not been attributed.
These runs checked initialization and rendering without comparing pixels or
measuring performance.

Evidence logs on the validation machine:

- OBS 30.2: `S:/Projects/obs-test/obs-30.2.0/config/obs-studio/logs/2026-10-28 23-58-53.txt`
- OBS 33: `%APPDATA%/obs-studio/logs/2026-10-28 23-56-31.txt`

The older host came from the official OBS release zip and ran in the approved
isolated test directory. Portable OBS ignores ProgramData plugin paths. Only the
test instances were closed. Original installed-OBS preferences were restored,
and the dedicated test collection was retained. The installed plugin now uses
the MSVC baseline DLL in the modern layout, with no legacy module copy beside it.

## Properties and compatibility checks

A separate process loaded the production DLL through the installed libobs API.
It checked the registered display name, Balanced/CK2/refiner defaults, property
order, three presets, two models, disabled manager button and `OBS_TEXT_INFO`
status. Both English and German labels passed. The check released the properties
and defaults and shut down libobs.

An earlier Clang 20.1.2 build compiled both translation units as C++20 against
the 30.2 and 33 headers with `-Wall -Wextra -Werror`. A diagnostic DLL redirected
only the version-query imports to a temporary shim reporting
`99.0.0-simulated`; all other OBS calls used the installed OBS 33 library. It
logged the unreviewed-version warning, loaded once, kept the filter active and
shut down normally. The fixture was removed and the normal DLL restored. This
checks version handling independently of missing required symbols; it does not
certify an actual future OBS ABI.

## Repository checks and remaining tests

clang-format 19.1.7, gersemi 0.22.2, actionlint 1.7.12 and Git whitespace checks
passed. Workflow/issue YAML, JSON and Python syntax were checked. All 16 locale
keys are present in both locales, and external actions use full commit hashes.
SPDX coverage passed; REUSE.toml supplies the declarations for valid JSON files.
The optional Windows ORT CMake scaffold configured and exposed its imported
target without starting the runtime build.

Linux compilation, hosted CI, installer execution and release packaging still
need verification. The workflows are ready for the coordinator's push; no push
or release publication was performed. Optional ORT builds, execution providers,
model downloads and inference were not run. The engine and model manager are
not implemented in this foundation.
