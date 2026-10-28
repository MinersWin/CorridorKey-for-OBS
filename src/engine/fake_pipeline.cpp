// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "fake_pipeline.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace corridorkey {
namespace {
constexpr std::uint64_t maximum_fake_pixels = 1024 * 1024;
Image image(Extent size, PixelFormat format, ColorSpace color, AlphaMode alpha, std::vector<float> values)
{
	const auto channels = format == PixelFormat::rgb_f32 ? 3U : 1U;
	return {size,
		format,
		color,
		alpha,
		static_cast<std::size_t>(size.width) * channels * sizeof(float),
		std::make_shared<const HostFloats>(std::move(values))};
}
Error synthesize(FrameResult &result, const FakeOptions &options)
{
	const auto &input = result.input.analysis;
	const auto pixels = static_cast<std::uint64_t>(input.size.width) * input.size.height;
	if (!input.storage || input.storage->domain() != MemoryDomain::host || input.format != PixelFormat::rgb_f32 ||
	    input.color != ColorSpace::srgb || !pixels || pixels > maximum_fake_pixels ||
	    input.row_stride_bytes != static_cast<std::size_t>(input.size.width) * 3 * sizeof(float))
		return error(ErrorCode::invalid_input);
	const auto rgb = input.storage->host_floats();
	if (rgb.size() != pixels * 3)
		return error(ErrorCode::invalid_input);
	for (float value : rgb)
		if (!std::isfinite(value) || value < 0 || value > 1)
			return error(ErrorCode::invalid_input);
	const bool inject = options.fault_sequence == 0 || options.fault_sequence == result.identity.sequence;
	if (inject && options.fault == FakeFault::out_of_memory)
		return error(ErrorCode::out_of_memory);
	if (inject && options.fault == FakeFault::backend_unavailable)
		return error(ErrorCode::backend_unavailable);
	std::vector<float> alpha(static_cast<std::size_t>(pixels));
	std::vector<float> foreground(rgb.begin(), rgb.end());
	for (std::size_t i = 0; i < alpha.size(); ++i)
		alpha[i] = std::clamp((rgb[3 * i] + rgb[3 * i + 2]) * 0.5f, 0.0f, 1.0f);
	if (inject && options.fault == FakeFault::invalid_output)
		alpha[0] = std::numeric_limits<float>::quiet_NaN();
	result.foreground =
		image(input.size, PixelFormat::rgb_f32, ColorSpace::srgb, AlphaMode::straight, std::move(foreground));
	result.alpha = image(input.size, PixelFormat::alpha_f32, ColorSpace::linear_srgb, AlphaMode::straight,
			     std::move(alpha));
	return validate_result(result);
}
} // namespace
Image synthetic_rgb(Extent size)
{
	const auto count = static_cast<std::uint64_t>(size.width) * size.height;
	if (!count || count > maximum_fake_pixels)
		throw std::invalid_argument("Synthetic input must contain 1 to 1048576 pixels");
	std::vector<float> rgb(static_cast<std::size_t>(count) * 3);
	for (std::uint32_t y = 0; y < size.height; ++y) {
		for (std::uint32_t x = 0; x < size.width; ++x) {
			const auto i = (static_cast<std::size_t>(y) * size.width + x) * 3;
			rgb[i] = static_cast<float>(x) / static_cast<float>(std::max(1U, size.width - 1));
			rgb[i + 1] = static_cast<float>(y) / static_cast<float>(std::max(1U, size.height - 1));
			rgb[i + 2] = ((x / 8 + y / 8) % 2) ? 1.0f : 0.0f;
		}
	}
	return image(size, PixelFormat::rgb_f32, ColorSpace::srgb, AlphaMode::opaque, std::move(rgb));
}
FakePipeline::FakePipeline(FakeOptions options, FakeExecution execution) : options_(options), execution_(execution)
{
	configure(options);
	options_ = configured_options_;
	if (execution_ == FakeExecution::worker)
		worker_ = std::thread([this] { run(); });
}
FakePipeline::~FakePipeline()
{
	shutdown();
}
void FakePipeline::configure(FakeOptions options)
{
	std::lock_guard lock(mutex_);
	options.delay = std::clamp(options.delay, std::chrono::milliseconds(0), std::chrono::milliseconds(30000));
	options.reorder_delay =
		std::clamp(options.reorder_delay, std::chrono::milliseconds(0), std::chrono::milliseconds(30000));
	configured_options_ = options;
}
void FakePipeline::clear_queued()
{
	for (auto &job : jobs_) {
		if (job) {
			job.reset();
			--diagnostics_.outstanding;
		}
	}
	for (auto &result : results_)
		result.reset();
	diagnostics_.outstanding -= result_count_;
	result_count_ = result_read_ = result_write_ = 0;
}
Error FakePipeline::prepare(const PrepareRequest &request)
{
	std::lock_guard lock(mutex_);
	if (stopping_ || request.cancellation.stop_requested())
		return error(ErrorCode::cancelled);
	if (!request.settings || !request.instance || !request.generation)
		return error(ErrorCode::invalid_input);
	if (configured_options_.fault == FakeFault::backend_unavailable && configured_options_.fault_sequence == 0)
		return error(ErrorCode::backend_unavailable);
	if (instance_ && (request.instance != instance_ || request.generation <= diagnostics_.generation))
		return error(ErrorCode::stale_generation);
	clear_queued();
	options_ = configured_options_;
	instance_ = request.instance;
	diagnostics_.generation = request.generation;
	diagnostics_.lifecycle = Lifecycle::ready;
	diagnostics_.failure = {};
	preparation_cancel_ = request.cancellation;
	cancelled_.store(false);
	last_sequence_ = last_tick_ = 0;
	wake_.notify_one();
	return {};
}
Error FakePipeline::reset(Generation generation, ResetReason)
{
	std::lock_guard lock(mutex_);
	if (stopping_ || !instance_)
		return error(ErrorCode::cancelled);
	if (generation <= diagnostics_.generation)
		return error(ErrorCode::stale_generation);
	if (configured_options_.fault == FakeFault::backend_unavailable && configured_options_.fault_sequence == 0)
		return error(ErrorCode::backend_unavailable);
	clear_queued();
	options_ = configured_options_;
	diagnostics_.generation = generation;
	diagnostics_.lifecycle = Lifecycle::ready;
	diagnostics_.failure = {};
	cancelled_.store(false);
	last_sequence_ = last_tick_ = 0;
	wake_.notify_one();
	return {};
}
Error FakePipeline::try_submit(InputFrame &input, TimePoint now) noexcept
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock())
		return error(ErrorCode::busy);
	Error failure;
	if (stopping_ || cancelled_.load() || preparation_cancel_.stop_requested() ||
	    input.cancellation.stop_requested())
		failure = error(ErrorCode::cancelled);
	else if (input.identity.generation != diagnostics_.generation || input.identity.instance != instance_)
		failure = error(ErrorCode::stale_generation);
	else if ((failure = validate_input(input))) {
	} else if (input.capture_complete && !input.capture_complete->ready()) {
		failure = error(ErrorCode::busy);
	} else if (now < input.identity.captured_at || input.identity.sequence <= last_sequence_ ||
		   (last_sequence_ && input.identity.video_tick == last_tick_))
		failure = error(ErrorCode::invalid_input);
	else if (diagnostics_.outstanding >= FramePool::capacity)
		failure = error(ErrorCode::busy);
	if (failure) {
		++diagnostics_.rejected;
		return failure;
	}
	for (auto &slot : jobs_) {
		if (!slot) {
			const auto extra = input.identity.sequence % 2 ? options_.reorder_delay
								       : std::chrono::milliseconds::zero();
			input.lease.transition(FrameState::processing);
			last_sequence_ = input.identity.sequence;
			last_tick_ = input.identity.video_tick;
			slot.emplace(Job{std::move(input), now, now + options_.delay + extra, options_,
					 preparation_cancel_});
			++diagnostics_.accepted;
			++diagnostics_.outstanding;
			wake_.notify_one();
			return {};
		}
	}
	return error(ErrorCode::busy);
}
std::optional<FrameResult> FakePipeline::poll_result() noexcept
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || !result_count_)
		return std::nullopt;
	auto result = std::move(results_[result_read_]);
	if (cancelled_.load() || preparation_cancel_.stop_requested() || result->input.cancellation.stop_requested())
		result->failure = error(ErrorCode::cancelled);
	results_[result_read_].reset();
	result_read_ = (result_read_ + 1) % FramePool::capacity;
	--result_count_;
	--diagnostics_.outstanding;
	return result;
}
void FakePipeline::cancel() noexcept
{
	cancelled_.store(true);
	wake_.notify_one();
}
bool FakePipeline::process_one(TimePoint now)
{
	std::optional<Job> job;
	{
		std::lock_guard lock(mutex_);
		if (stopping_)
			return false;
		std::optional<Job> *selected = nullptr;
		for (auto &candidate : jobs_) {
			if (!candidate)
				continue;
			// Capture ownership is retained even after cancellation until its fence signals.
			if (candidate->input.capture_complete && !candidate->input.capture_complete->ready())
				continue;
			const bool cancelled = cancelled_.load() || candidate->input.cancellation.stop_requested() ||
					       candidate->preparation_cancel.stop_requested();
			if (!cancelled && candidate->due_at > now)
				continue;
			if (!selected || candidate->due_at < (*selected)->due_at ||
			    (candidate->due_at == (*selected)->due_at &&
			     candidate->input.identity.sequence < (*selected)->input.identity.sequence))
				selected = &candidate;
		}
		if (!selected)
			return false;
		job = std::move(*selected);
		selected->reset();
	}
	FrameResult result;
	result.identity = job->input.identity;
	result.transform = job->input.transform;
	result.input = std::move(job->input);
	result.timings.submitted_at = job->submitted_at;
	result.timings.started_at = job->submitted_at;
	result.timings.completed_at = now;
	result.timings.inference = now - job->submitted_at;
	try {
		if (cancelled_.load() || result.input.cancellation.stop_requested() ||
		    job->preparation_cancel.stop_requested())
			result.failure = error(ErrorCode::cancelled);
		else
			result.failure = synthesize(result, job->options);
	} catch (const std::bad_alloc &) {
		result.failure = error(ErrorCode::out_of_memory);
	} catch (...) {
		result.failure = error(ErrorCode::internal);
	}
	std::lock_guard lock(mutex_);
	if (stopping_ || result.identity.generation != diagnostics_.generation || !result.input.lease.current()) {
		--diagnostics_.outstanding;
		return true;
	}
	if (cancelled_.load() || result.input.cancellation.stop_requested() || job->preparation_cancel.stop_requested())
		result.failure = error(ErrorCode::cancelled);
	result.input.lease.transition(FrameState::complete);
	diagnostics_.failure = result.failure;
	++diagnostics_.completed;
	results_[result_write_] = std::move(result);
	result_write_ = (result_write_ + 1) % FramePool::capacity;
	++result_count_;
	return true;
}
void FakePipeline::advance(TimePoint now)
{
	if (execution_ != FakeExecution::manual)
		throw std::logic_error("advance is only available in manual execution mode");
	while (process_one(now)) {
	}
}
void FakePipeline::run()
{
	for (;;) {
		while (process_one(Clock::now())) {
		}
		std::unique_lock lock(mutex_);
		if (stopping_)
			return;
		const auto pending = [this] {
			return stopping_ ||
			       std::any_of(jobs_.begin(), jobs_.end(), [](const auto &job) { return job.has_value(); });
		};
		if (pending())
			wake_.wait_for(lock, std::chrono::milliseconds(1));
		else
			wake_.wait(lock, pending);
	}
}
void FakePipeline::shutdown() noexcept
{
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
		cancelled_.store(true);
	}
	wake_.notify_one();
	if (worker_.joinable())
		worker_.join();
	std::lock_guard lock(mutex_);
	clear_queued();
	diagnostics_.lifecycle = Lifecycle::stopped;
}
PipelineCapabilities FakePipeline::capabilities() const
{
	PipelineCapabilities result;
	result.backend = Backend::fake;
	result.transport = TransportKind::synthetic;
	result.synthetic = true;
	result.color_spaces = {ColorSpace::srgb};
	return result;
}
PipelineDiagnostics FakePipeline::diagnostics() const
{
	std::lock_guard lock(mutex_);
	auto result = diagnostics_;
	if (cancelled_.load() && result.lifecycle != Lifecycle::stopped)
		result.lifecycle = Lifecycle::stopping;
	return result;
}
} // namespace corridorkey
