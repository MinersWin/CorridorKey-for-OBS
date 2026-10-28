// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "types.hpp"
#include <memory>
#include <optional>
#include <string>

namespace corridorkey {
enum class Lifecycle { idle, preparing, warming, ready, bypassed, stopping, failed, stopped };
enum class ModelState {
	missing,
	awaiting_acceptance,
	downloading,
	verifying,
	binding,
	building,
	warming,
	ready,
	update_available,
	failed
};
struct Progress {
	std::uint64_t completed_bytes = 0;
	std::optional<std::uint64_t> total_bytes;
	bool can_cancel_now = true;
};
struct Status {
	Lifecycle lifecycle = Lifecycle::idle;
	Generation generation = 0;
	Backend backend = Backend::automatic;
	TransportKind transport = TransportKind::automatic;
	Error failure;
	std::string diagnostic_detail;
	std::string model_id, model_revision;
	std::optional<double> inference_median_ms, age_median_ms, age_p95_ms, keyed_fps;
	std::uint64_t submitted = 0, completed = 0, dropped = 0, repeated = 0;
	bool untested_combination = false;
};
using StatusSnapshot = std::shared_ptr<const Status>;
} // namespace corridorkey
