// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "model_pipeline.hpp"
#include "ck1_sequence.hpp"
#include "refiner_stage.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace corridorkey {
struct Ck1Prepared {
	ck1::Graphs graphs;
	Backend backend = Backend::automatic;
	std::string detail;
	std::shared_ptr<const ReadyModel> model{};
};
using Ck1Factory = std::function<Ck1Prepared(const PrepareRequest &)>;
class Ck1Pipeline final : public ModelPipeline {
public:
	explicit Ck1Pipeline(Ck1Factory, std::function<TimePoint()> now = Clock::now);
	~Ck1Pipeline() override;
	Error prepare(const PrepareRequest &) override;
	Error reset(Generation, ResetReason) override;
	Error try_submit(InputFrame &, TimePoint) noexcept override;
	std::optional<FrameResult> poll_result() noexcept override;
	void cancel() noexcept override;
	void shutdown() noexcept override;
	PipelineCapabilities capabilities() const override;
	PipelineDiagnostics diagnostics() const override;
	Crop suggested_roi() const override;
	ck2::StageTimes stage_times() const override;
	std::string detail() const override;
	std::shared_ptr<refiner::Stage> refiner_stage() const override;
	std::shared_ptr<nvidia::Context> device_context() const override;
	static bool supports(const Settings &);

private:
	struct Job {
		InputFrame frame;
		TimePoint submitted;
	};
	void work();
	void clear();
	Ck1Factory factory_;
	std::shared_ptr<nvidia::Context> device_context_;
	std::function<TimePoint()> now_;
	mutable std::mutex mutex_;
	std::mutex execution_;
	std::condition_variable wake_;
	std::thread worker_;
	std::optional<Job> pending_;
	std::optional<FrameResult> result_;
	std::unique_ptr<ck1::SequenceState> sequence_;
	std::shared_ptr<const ReadyModel> model_;
	std::stop_token preparation_stop_;
	PipelineDiagnostics diagnostics_;
	PipelineCapabilities capabilities_;
	InstanceId instance_ = 0;
	Sequence last_sequence_ = 0;
	std::uint64_t last_tick_ = 0;
	unsigned consecutive_failures_ = 0;
	Crop roi_;
	ck2::StageTimes stage_times_;
	std::string detail_;
	std::atomic<bool> cancelled_{false}, stopping_{false};
};
} // namespace corridorkey
