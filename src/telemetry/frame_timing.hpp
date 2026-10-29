// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/frame.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <optional>

namespace corridorkey {
struct TimingSnapshot {
	std::optional<double> age_median_ms, age_p95_ms, inference_median_ms;
	std::optional<double> input_transfer_median_ms, output_transfer_median_ms, queue_median_ms, keyed_fps;
	std::uint64_t presentations = 0, repeated = 0;
	TimePoint last_presented{}, window_start{};
};
// One sample per OBS video tick, including repeated outputs. No allocation in record().
class FrameTiming {
public:
	void record(const FrameResult &frame, TimePoint now, std::uint64_t tick) noexcept;
	TimingSnapshot snapshot(TimePoint now) const;
	void reset() noexcept;

private:
	struct Sample {
		TimePoint at;
		double age = 0, inference = 0, input = 0, output = 0, queue = 0;
		bool fresh = false;
	};
	mutable std::mutex mutex_;
	std::atomic<bool> reset_requested_{false};
	std::array<Sample, 1024> samples_{};
	std::size_t next_ = 0, count_ = 0;
	Generation generation_ = 0;
	Sequence sequence_ = 0;
	std::optional<std::uint64_t> tick_;
};
} // namespace corridorkey
