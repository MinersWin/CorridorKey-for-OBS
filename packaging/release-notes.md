<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
Install with OBS closed. The unsigned Windows installer includes ONNX Runtime and
DirectML. Use the modern zip for portable OBS 33+, or the legacy zip for OBS
30.2–32. Linux packages are separate for Ubuntu 24.04 and 26.04; use the package
that matches your distribution and host Qt. Flatpak is not supported.

Models and NVIDIA libraries are separate downloads and require licence acceptance.
No model weights, model graphs or compiled engines are included. The plugin
verifies the bundled model catalogue against its embedded release public key.

Verify SHA256SUMS.txt and read the bundled THIRD_PARTY_NOTICES and BUNDLE_CONTENTS.
See [installation](https://github.com/MinersWin/CorridorKey-for-OBS/blob/main/docs/user/install.md)
for portable installs, upgrades, rollback and uninstall. Maintainers must review the
validation notes and outstanding release checks before publishing this draft.
