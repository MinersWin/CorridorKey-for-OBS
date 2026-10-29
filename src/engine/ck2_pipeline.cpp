// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck2_pipeline.hpp"
#include <stdexcept>

namespace corridorkey {
Ck2Pipeline::Ck2Pipeline(Ck2Factory factory, std::function<TimePoint()> now)
	: factory_(std::move(factory)),
	  now_(std::move(now))
{
	if (!now_)
		throw std::invalid_argument("Missing pipeline clock");
	worker_ = std::thread([this] { work(); });
}
Ck2Pipeline::~Ck2Pipeline()
{
	shutdown();
}
void Ck2Pipeline::clear()
{
	pending_.reset();
	result_.reset();
	diagnostics_.outstanding = 0;
	last_sequence_ = last_tick_ = 0;
	consecutive_failures_ = 0;
}
bool Ck2Pipeline::supports(const Settings &settings)
{
	return settings.quality.model == ModelFamily::ck2 && !settings.quality.aspect_fix &&
	       !settings.experimental.head_pass && !settings.experimental.stagger_stages &&
	       settings.hint.ck2_mode == HintMode::off &&
	       (!settings.quality.refiner_enabled || (refiner::supported_budget(settings.quality.refiner_max_tiles) &&
						      (settings.quality.guide_mode == GuideMode::refiner ||
						       settings.quality.guide_mode == GuideMode::rgb_guided) &&
						      settings.quality.unmix_mode == UnmixMode::delta));
}
Error Ck2Pipeline::prepare(const PrepareRequest &request)
{
	if (!request.instance || !request.generation || !request.settings)
		return error(ErrorCode::invalid_input);
	if (!supports(*request.settings))
		return error(ErrorCode::unsupported);
	if (stopping_ || request.cancellation.stop_requested())
		return error(ErrorCode::cancelled);
	try {
		auto prepared = factory_(request);
		auto device_context = prepared.graphs.at(ck2::Stage::stage1)->device_context();
		auto model = prepared.model ? prepared.model : request.model;
		std::shared_ptr<refiner::Stage> refinement;
		if (auto found = prepared.graphs.find(ck2::Stage::refiner); found != prepared.graphs.end()) {
			refinement = std::make_shared<refiner::Stage>(std::move(found->second),
								      request.settings->quality.refiner_max_tiles,
								      model ? model->lease : nullptr);
			prepared.graphs.erase(found);
		}
		auto sequence = std::make_unique<ck2::SequenceState>(
			request.settings->quality.grid, request.settings->quality.profile == Profile::quality,
			std::move(prepared.graphs));
		std::lock_guard execution(execution_);
		std::lock_guard lock(mutex_);
		if (stopping_ || request.cancellation.stop_requested())
			return error(ErrorCode::cancelled);
		if (instance_ && (request.instance != instance_ || request.generation <= diagnostics_.generation))
			return error(ErrorCode::stale_generation);
		clear();
		sequence_ = std::move(sequence);
		device_context_ = std::move(device_context);
		refiner_ = std::move(refinement);
		model_ = std::move(model);
		instance_ = request.instance;
		preparation_stop_ = request.cancellation;
		diagnostics_.generation = request.generation;
		diagnostics_.lifecycle = Lifecycle::ready;
		diagnostics_.failure = {};
		capabilities_.backend = prepared.backend;
		capabilities_.transport = TransportKind::staged_grid;
		capabilities_.temporal = true;
		capabilities_.stage2 = request.settings->quality.profile == Profile::quality;
		capabilities_.grids = {request.settings->quality.grid};
		capabilities_.color_spaces = {ColorSpace::srgb};
		roi_ = request.settings->context.crop;
		detail_ = std::move(prepared.detail);
		cancelled_ = false;
		return {};
	} catch (const std::bad_alloc &) {
		return error(ErrorCode::out_of_memory);
	} catch (const std::exception &e) {
		std::lock_guard lock(mutex_);
		detail_ = e.what();
		return error(ErrorCode::backend_unavailable);
	}
}
Error Ck2Pipeline::reset(Generation generation, ResetReason)
{
	std::lock_guard execution(execution_);
	std::lock_guard lock(mutex_);
	if (!sequence_ || stopping_)
		return error(ErrorCode::cancelled);
	if (generation <= diagnostics_.generation)
		return error(ErrorCode::stale_generation);
	clear();
	sequence_->reset();
	diagnostics_.generation = generation;
	diagnostics_.failure = {};
	diagnostics_.lifecycle = Lifecycle::ready;
	cancelled_ = false;
	preparation_stop_ = {};
	return {};
}
Error Ck2Pipeline::try_submit(InputFrame &input, TimePoint now) noexcept
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock())
		return error(ErrorCode::busy);
	Error problem;
	if (cancelled_ || stopping_ || preparation_stop_.stop_requested() || input.cancellation.stop_requested())
		problem = error(ErrorCode::cancelled);
	else if (!sequence_)
		problem = error(ErrorCode::model_missing);
	else if (input.settings && !supports(*input.settings))
		problem = error(ErrorCode::unsupported);
	else if (diagnostics_.lifecycle == Lifecycle::failed)
		problem = diagnostics_.failure;
	else if (input.identity.instance != instance_ || input.identity.generation != diagnostics_.generation)
		problem = error(ErrorCode::stale_generation);
	else if ((problem = validate_input(input))) {
	} else if (input.capture_complete && !input.capture_complete->ready())
		problem = error(ErrorCode::busy);
	else if (input.identity.sequence <= last_sequence_ ||
		 (last_sequence_ && input.identity.video_tick == last_tick_) || now < input.identity.captured_at)
		problem = error(ErrorCode::invalid_input);
	else if (diagnostics_.outstanding)
		problem = error(ErrorCode::busy);
	if (problem) {
		++diagnostics_.rejected;
		return problem;
	}
	if (!input.lease.transition(FrameState::processing))
		return error(ErrorCode::invalid_input);
	last_sequence_ = input.identity.sequence;
	last_tick_ = input.identity.video_tick;
	pending_.emplace(Job{std::move(input), now});
	++diagnostics_.accepted;
	++diagnostics_.outstanding;
	wake_.notify_one();
	return {};
}
std::optional<FrameResult> Ck2Pipeline::poll_result() noexcept
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || !result_)
		return {};
	auto result = std::move(result_);
	result_.reset();
	--diagnostics_.outstanding;
	if (cancelled_ || preparation_stop_.stop_requested() || result->input.cancellation.stop_requested())
		result->failure = error(ErrorCode::cancelled);
	return result;
}
void Ck2Pipeline::work()
{
	for (;;) {
		{
			std::unique_lock lock(mutex_);
			wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
			if (stopping_)
				return;
		}
		std::lock_guard execution(execution_);
		std::optional<Job> job;
		{
			std::lock_guard lock(mutex_);
			if (!pending_)
				continue;
			job = std::move(pending_);
			pending_.reset();
		}
		FrameResult output;
		output.input = std::move(job->frame);
		output.identity = output.input.identity;
		output.transform = output.input.transform;
		output.timings.submitted_at = job->submitted;
		output.timings.started_at = now_();
		std::optional<ck2::LowResolutionResult> decoded;
		try {
			if (cancelled_ || stopping_ || preparation_stop_.stop_requested())
				output.failure = error(ErrorCode::cancelled);
			else {
				decoded = sequence_->process(output.input, output.input.cancellation, &cancelled_);
				output.foreground = ck2::packed_image(decoded->foreground, PixelFormat::rgb_f32);
				output.alpha = ck2::packed_image(decoded->alpha, PixelFormat::alpha_f32);
				output.failure = validate_result(output);
			}
		} catch (const std::bad_alloc &) {
			output.failure = error(ErrorCode::out_of_memory);
		} catch (const std::invalid_argument &) {
			output.failure = error(ErrorCode::invalid_input);
		} catch (...) {
			output.failure = error(ErrorCode::invalid_output);
		}
		output.timings.completed_at = now_();
		output.timings.inference = output.timings.completed_at - output.timings.started_at;
		std::lock_guard lock(mutex_);
		if (cancelled_ || stopping_ || preparation_stop_.stop_requested() ||
		    output.input.cancellation.stop_requested())
			output.failure = error(ErrorCode::cancelled);
		if (output.failure) {
			sequence_->reset();
			if (output.failure.code != ErrorCode::cancelled && ++consecutive_failures_ >= 3)
				diagnostics_.lifecycle = Lifecycle::failed;
		} else if (decoded) {
			roi_ = decoded->next_roi;
			stage_times_ = decoded->timings;
			consecutive_failures_ = 0;
		}
		output.input.lease.transition(FrameState::complete);
		diagnostics_.failure = output.failure;
		++diagnostics_.completed;
		result_ = std::move(output);
	}
}
void Ck2Pipeline::cancel() noexcept
{
	cancelled_ = true;
	wake_.notify_one();
}
void Ck2Pipeline::shutdown() noexcept
{
	stopping_ = true;
	cancel();
	wake_.notify_one();
	if (worker_.joinable())
		worker_.join();
	std::lock_guard lock(mutex_);
	clear();
	sequence_.reset();
	refiner_.reset();
	model_.reset();
	diagnostics_.lifecycle = Lifecycle::stopped;
}
PipelineCapabilities Ck2Pipeline::capabilities() const
{
	std::lock_guard lock(mutex_);
	return capabilities_;
}
PipelineDiagnostics Ck2Pipeline::diagnostics() const
{
	std::lock_guard lock(mutex_);
	return diagnostics_;
}
Crop Ck2Pipeline::suggested_roi() const
{
	std::lock_guard lock(mutex_);
	return roi_;
}
ck2::StageTimes Ck2Pipeline::stage_times() const
{
	std::lock_guard lock(mutex_);
	return stage_times_;
}
std::string Ck2Pipeline::detail() const
{
	std::lock_guard lock(mutex_);
	return detail_;
}
std::shared_ptr<refiner::Stage> Ck2Pipeline::refiner_stage() const
{
	std::lock_guard lock(mutex_);
	return refiner_;
}
std::shared_ptr<nvidia::Context> Ck2Pipeline::device_context() const
{
	std::lock_guard lock(mutex_);
	return device_context_;
}
} // namespace corridorkey
