<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
The model service validates signed catalogues and pinned manifests, records exact
licence acceptance, downloads or imports authenticated blobs, and protects cached
revisions with process locks and reader leases. See
[the author and integration guide](../../docs/dev/models-catalogue.md).

`catalog.hpp` implements the shared `ModelCatalog` contract and uses the
[native binder](../../docs/dev/binder.md) by default. W07 supplies the licence/installation UI. The OBS adapter
in `cache_obs.cpp` resolves the configuration directory without adding OBS to the
portable core. Graphs and checkpoints remain outside this repository.
