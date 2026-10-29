<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
The model manager shares one modeless Qt window across Tools and filter properties.
Keep cache and installation work in `ManagerController`; widget code renders snapshots
and sends commands. Licence acceptance precedes downloads and offline import.
See [integration notes](../../docs/dev/model-manager.md) for the binder, probe,
diagnostics and release-catalogue seams.
