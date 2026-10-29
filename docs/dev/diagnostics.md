<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Diagnostics collector

`telemetry/diagnostics.*` builds a versioned JSON support report from typed
snapshots. The manager opens a modeless preview; only the clipboard button
copies its text. Report a problem opens a fixed GitHub issue-chooser URL with
no query parameters. Neither action submits a report or uploads an OBS log.

## Inputs and privacy

| Input | Reported fields | Excluded fields |
| --- | --- | --- |
| Build and host | Plugin version and configure-time commit, OBS/renderer, OS edition, kernel, architecture, runtime Qt, canvas/FPS, streaming/recording | Hostname, account name, environment dump |
| GPUs | GPU name, driver, vendor/device IDs, LUID on Windows or PCI identity on Linux, dedicated VRAM when available | Source/device labels from OBS |
| W06 runtime descriptors and completed tests | Bundle ID, ORT/EP build, component hashes, negotiated API, probe/adapter-match flags and a fixed reason | Library paths, provider options, raw helper JSON and arbitrary diagnostic text |
| W07 manager snapshot | Package/revision, manifest/graph/weights hashes, graph revision, state and verification result, cache byte count | Display names, descriptions, URLs, licence text, import/cache paths |
| Per-filter snapshots | Source dimensions, selected backend/transport, lifecycle, model revision, numeric/bool settings and recognized choice values | All free-text settings, source UUIDs, audio pairing and mask-source IDs |
| W15 status | Displayed-age median/p95; inference, input/output transfer and queue medians; processed FPS and counters | Timestamp epochs, frames, audio |
| Error history | Up to 16 recently observed error-code changes with static recovery text | Error `action` supplied by a caller, raw errors, logs or stack traces |

The settings collector walks W02's field inventory. It copies numbers and
booleans; a string must match one of that field's declared choices. New text
settings therefore stay private without needing another exclusion entry.

Metadata passes through redaction before it reaches the preview. Known home,
profile and application-data prefixes become `<profile>`; Windows `Users`,
Linux `/home` and macOS `/Users` paths also lose their account component.
Credential patterns, bearer values and token/password/API-key assignments become
`<secret>`. IDs reject path/URL syntax, control characters become spaces and
metadata strings have a length limit. Path-like identities are removed entirely.
The cache path is always `<model-cache>`. Source/scene names are always omitted;
there is no name-sharing opt-in in this version.

These rules supplement the typed allowlist. They are not a general-purpose
redactor for arbitrary OBS logs. Review the preview and any separately attached
log before posting it.

## Runtime meaning

A descriptor means a bundle is configured; it does not mean its DLLs loaded or
its hashes were checked again by diagnostics. The report labels descriptor
hashes accordingly. A completed backend test supplies the negotiated ORT core,
EP version and result. No test runs just because the preview opens, and a missing
bundle is reported as unavailable. No runtime library is loaded for collection.

`BackendTest::poll` records the latest completed result per backend. CK2 activation
also calls `diagnostics::record_probe` through its preparation callback. The
collector discards raw helper details and arbitrary
error actions at that seam. A tiny graph probe remains distinct from full-model
and native-video-transport validation.

Model hashes come from admitted manifests. Ready means W07 verified the revision;
other states do not imply verification. Collection does not rehash model weights
or traverse the cache on the UI thread. The build commit comes from CMake's
configure step and includes `-dirty` for an uncommitted build; reconfigure after
changing commits.

Filters register control-side snapshot callbacks after construction and remove
them before teardown. Collection holds the registry lock while reading them,
so a callback cannot outlive its filter. It reads settings/status, not Qt labels.
Error recording uses a bounded atomic ring and never copies caller text. Filter
status observations and completed backend tests feed that ring; it is not a
complete OBS log or a history of every transient error between observations.

W15 expires timing values after 250 ms without a successful presentation. Its
rolling window holds up to five seconds or 1024 presentations. Missing or
nonfinite timing values are JSON `null`; they are not zero-latency claims.
Submission/completion/drop counters retain the producing pipeline's scope.

Linux GPU inventory reads kernel DRM/sysfs identities and driver metadata. A
missing driver version falls back to the kernel version; a GPU name may appear
as its PCI vendor/device pair. This path still needs a real Linux-host check.

## Compatibility checks

`host_policy.*` separates unfamiliar version numbers from missing required ABI
symbols. It checks required registration/version exports and warns outside the
reviewed host range. Optional `obs_get_active_fps` resolves at runtime. Without
it, diagnostics uses the configured rational FPS; an invalid live value also
uses that fallback. This is an advisory version policy, not an upper version pin.

The OS loader still resolves the plugin's normal imports before module startup.
A missing required import or incompatible Qt/C++ ABI can fail there, before our
log message is possible. Unit tests mock missing required exports; they do not
claim to load a binary with a deliberately corrupted ABI into OBS.

The portable diagnostics test target joins the existing Windows/Linux Contracts
workflow. See [W17 validation](validation-w17.md) for the actual runs and
[compatibility](../compatibility.md) for evidence labels.
