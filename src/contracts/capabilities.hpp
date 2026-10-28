// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "frame.hpp"
#include <string>
#include <vector>

namespace corridorkey {
struct AdapterIdentity {
	std::string stable_id; // LUID, UUID or PCI identity; empty means unknown.
	std::string display_name;
	std::uint32_t vendor_id = 0, device_id = 0;
};
struct ProviderCapabilities {
	Backend backend = Backend::automatic;
	AdapterIdentity adapter;
	bool available = false, matches_obs_adapter = false, tested = false;
	bool permits_live_inference = false, io_binding = false, cooperative_cancel = false;
	std::vector<Precision> precisions;
	std::vector<MemoryDomain> tensor_domains;
	std::string runtime_build, provider_build;
	Error unavailable_reason;
};
struct TransportCapabilities {
	TransportKind kind = TransportKind::automatic;
	MemoryDomain domain = MemoryDomain::host;
	bool available = false, nonblocking_capture = false, native_rgb = false, native_tiles = false;
	AdapterIdentity adapter;
	Error unavailable_reason;
};
struct PipelineCapabilities {
	Backend backend = Backend::automatic;
	TransportKind transport = TransportKind::automatic;
	bool synthetic = false, temporal = false, stage2 = false, refiner = false;
	bool head_pass = false, stagger_stages = false, ck1_refiner_validated = false;
	std::vector<Extent> grids;
	std::vector<std::uint32_t> tile_batches;
	std::vector<HintSource> hint_sources;
	std::vector<ColorSpace> color_spaces;
};
struct ProbeRequest {
	Backend backend = Backend::automatic;
	AdapterIdentity obs_adapter;
	Precision precision = Precision::automatic;
	std::string package_id;
	std::chrono::milliseconds timeout{10000};
};
struct ProbeResult {
	ProviderCapabilities provider;
	std::vector<TransportCapabilities> transports;
	Error failure;
	std::string diagnostic_detail; // Redacted; never contains frames or personal paths.
	bool timed_out = false, process_crashed = false;
	std::uint32_t negotiated_api = 0;
};
class ProviderProbe {
public:
	virtual ~ProviderProbe() = default;
	// Loader/helper context only; implementations contain process failures.
	virtual ProbeResult probe(const ProbeRequest &, std::stop_token) = 0;
};
class FrameTransport {
public:
	virtual ~FrameTransport() = default;
	virtual TransportCapabilities capabilities() const = 0; // Control thread.
	// Render-safe try operations; input remains owned by caller on failure.
	virtual Error try_capture(InputFrame &input) noexcept = 0;
	virtual bool capture_ready(const InputFrame &input) noexcept = 0;
	// On success, retain the result until read_complete signals (null = synchronous).
	virtual Error try_present(const FrameResult &result,
				  std::shared_ptr<const Completion> &read_complete) noexcept = 0;
};
} // namespace corridorkey
