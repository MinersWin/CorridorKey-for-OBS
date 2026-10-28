// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "pipeline.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace corridorkey {
enum class FakeFault { none, out_of_memory, backend_unavailable, invalid_output };
struct FakeOptions {
	std::chrono::milliseconds delay{0};
	// Odd sequences take delay + reorder_delay; even sequences take delay.
	std::chrono::milliseconds reorder_delay{0};
	FakeFault fault = FakeFault::none;
	Sequence fault_sequence = 0; // Zero applies to every frame.
};
enum class FakeExecution { worker, manual };

class FakePipeline final : public Pipeline {
public:
	explicit FakePipeline(FakeOptions options = {}, FakeExecution execution = FakeExecution::worker);
	~FakePipeline() override;
	Error prepare(const PrepareRequest &) override;
	Error reset(Generation, ResetReason) override;
	Error try_submit(InputFrame &, TimePoint now) noexcept override;
	std::optional<FrameResult> poll_result() noexcept override;
	void cancel() noexcept override;
	void shutdown() noexcept override;
	PipelineCapabilities capabilities() const override;
	PipelineDiagnostics diagnostics() const override;
	void configure(FakeOptions options); // Control thread, before prepare/reset.
	void advance(TimePoint now);         // Deterministic manual mode; never render.

private:
	struct Job {
		InputFrame input;
		TimePoint submitted_at, due_at;
		FakeOptions options;
		std::stop_token preparation_cancel;
	};
	bool process_one(TimePoint now);
	void run();
	void clear_queued();
	FakeOptions options_;
	FakeOptions configured_options_;
	FakeExecution execution_;
	mutable std::mutex mutex_;
	std::condition_variable wake_;
	std::thread worker_;
	std::array<std::optional<Job>, FramePool::capacity> jobs_;
	std::array<std::optional<FrameResult>, FramePool::capacity> results_;
	std::size_t result_read_ = 0, result_write_ = 0, result_count_ = 0;
	PipelineDiagnostics diagnostics_;
	InstanceId instance_ = 0;
	Sequence last_sequence_ = 0;
	std::uint64_t last_tick_ = 0;
	std::stop_token preparation_cancel_;
	std::atomic<bool> cancelled_{false};
	bool stopping_ = false;
};

// Independently authored RGB ramp/checker fixture. Allocates only on control/tests.
Image synthetic_rgb(Extent size);
} // namespace corridorkey
