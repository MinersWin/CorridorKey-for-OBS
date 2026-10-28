// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "contracts/capabilities.hpp"
#include "contracts/models.hpp"
#include <optional>

namespace corridorkey {
struct PrepareRequest {
	InstanceId instance = 0;
	Generation generation = 0;
	SettingsSnapshot settings;
	std::shared_ptr<const ReadyModel> model;
	ProviderCapabilities provider;
	TransportCapabilities transport;
	std::stop_token cancellation;
};
struct PipelineDiagnostics {
	Lifecycle lifecycle = Lifecycle::idle;
	Generation generation = 0;
	std::uint64_t accepted = 0, completed = 0, rejected = 0;
	std::size_t outstanding = 0;
	Error failure;
};
class Pipeline {
public:
	virtual ~Pipeline() = default;
	// prepare/reset/shutdown/capabilities/diagnostics are control/worker calls.
	// Prepare a replacement separately; retain the active pipeline on failure.
	virtual Error prepare(const PrepareRequest &) = 0;
	virtual Error reset(Generation generation, ResetReason reason) = 0;
	// try_submit consumes input only on success; it never waits or allocates.
	virtual Error try_submit(InputFrame &input, TimePoint now) noexcept = 0;
	virtual std::optional<FrameResult> poll_result() noexcept = 0;
	virtual void cancel() noexcept = 0;
	virtual void shutdown() noexcept = 0; // May join. Never call from render.
	virtual PipelineCapabilities capabilities() const = 0;
	virtual PipelineDiagnostics diagnostics() const = 0;
};
} // namespace corridorkey
