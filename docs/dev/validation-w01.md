<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# W01 validation, 29 October 2026

Windows builds used MSVC 19.44, Windows SDK 10.0.26100.0 and the existing OBS
30.2 baseline dependencies. Qt and frontend support remained enabled; ONNX
Runtime fetching stayed off. No OBS dependencies were downloaded again.

- `windows-ci-x64` passed with `CORRIDORKEY_ENABLE_FAKE_FILTER=ON`.
- A separate build with the same preset and the fake option off passed.
- The portable core and tests built with `CORRIDORKEY_BUILD_PLUGIN=OFF`, without
  finding or linking OBS or Qt.
- CTest passed. The doctest executable reported 16 cases and 1,827 assertions.
  Coverage includes exact input/result identity, crop transforms, timestamps,
  simulated delay, reordered output, stale generations, pinned/reused slots,
  completed-queue bounds, injected failures, cancellation, duplicate ticks,
  invalid values, fences, independent instances and worker shutdown. A worker
  test also performs 200 generation swaps.
- The independent Add graph passed the ONNX checker and reference evaluator.
  Its generated external weights and safetensors fixture contain four authored
  constants. No model artifacts were used.
- clang-format 19.1.7, gersemi and Git whitespace checks passed for the changed
  code and build files. New source, test, script and documentation files carry
  SPDX headers. doctest's MIT and ONNX's Apache-2.0 licences were checked and
  added to the third-party notices. Documentation received the humanizer review.

The diagnostic DLL is `build_x64/RelWithDebInfo/corridorkey.dll`. The default
DLL is `build_passthrough/RelWithDebInfo/corridorkey.dll`. The contract-only test
executable is under `build_core/tests/RelWithDebInfo/`. These are local build
outputs and are not committed or installed into OBS.

Linux compilation and the new hosted CI workflow have not run locally. The core
uses standard C++20 types and threads; platform transports remain separate.
Live OBS rendering, fake-preview pixels, callback p95, source resize/delete under
active rendering and GPU fences have not been exercised by W01. The unchanged
passthrough begin/end path is build-checked, not newly certified by a host run.
W03 owns those capture and presentation checks. The fake's simulated durations
are not measured inference performance.

W02 still needs the full settings schema/migration and live status UI. W04/W06
must implement catalogue/probe services against the shared types. Real temporal
tensors, exported-model parity and provider recovery policy belong to their
later packages. There are no blocking contract questions for these four packages.
