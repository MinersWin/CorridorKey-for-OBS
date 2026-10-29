// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "filter_input.hpp"
#include "staged_transport.hpp"
#include "engine/fake_pipeline.hpp"
#include "runtime/scheduler.hpp"
#include "telemetry/frame_timing.hpp"
#include "telemetry/obs_clock.hpp"
#include <obs-module.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <limits>
#include <exception>

namespace corridorkey {
namespace {
FairAdmission admission;
std::atomic<InstanceId> next_instance{1};
struct GraphicsScope {
	GraphicsScope() { obs_enter_graphics(); }
	~GraphicsScope() { obs_leave_graphics(); }
};
// Fixed histograms avoid allocation/sorting/logging in video_render.
struct CallbackTimes {
	static constexpr std::size_t bins = 2001;
	std::array<std::atomic<std::uint64_t>, bins> total{}, self{};
	std::atomic<std::uint64_t> count{0};
	void record(Duration elapsed, Duration host) noexcept
	{
		auto index = [](Duration d) {
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
			return static_cast<std::size_t>(std::clamp<std::int64_t>(us / 10, 0, bins - 1));
		};
		++total[index(elapsed)];
		++self[index(elapsed - host)];
		++count;
	}
	static double quantile(const std::array<std::atomic<std::uint64_t>, bins> &hist, std::uint64_t n, double q)
	{
		std::uint64_t accumulated = 0;
		for (std::size_t i = 0; i < bins; ++i) {
			accumulated += hist[i].load();
			if (accumulated >= static_cast<std::uint64_t>(std::ceil(static_cast<double>(n) * q)))
				return i == bins - 1 ? std::numeric_limits<double>::infinity()
						     : static_cast<double>(i + 1) / 100.0;
		}
		return 20.01;
	}
};
std::vector<std::uint8_t> pack(const FrameResult &result)
{
	const auto rgb = result.foreground.storage->host_floats();
	const auto alpha = result.alpha.storage->host_floats();
	std::vector<std::uint8_t> rgba(alpha.size() * 4);
	for (std::size_t p = 0; p < alpha.size(); ++p) {
		for (unsigned c = 0; c < 3; ++c)
			rgba[p * 4 + c] = static_cast<std::uint8_t>(std::lround(rgb[p * 3 + c] * 255));
		rgba[p * 4 + 3] = static_cast<std::uint8_t>(std::lround(alpha[p] * 255));
	}
	return rgba;
}
} // namespace
struct FilterInput::Impl {
	obs_source_t *source;
	const InstanceId instance = next_instance.fetch_add(1);
	mutable std::mutex mutex;
	FramePool pool;
	FakePipeline pipeline{{}, FakeExecution::manual};
	Scheduler scheduler{admission}; // Worker only; render deduplication is separate.
	SettingsSnapshot settings;
	FakeOptions options;
	std::unique_ptr<StagedTransport> transport;
	std::optional<FrameResult> presented;
	std::array<std::optional<FrameResult>, FramePool::capacity> retired;
	Extent size;
	obs_weak_source_t *target = nullptr;
	bool enabled = false, host_enabled = true;
	std::uint64_t last_capture_tick = 0;
	bool has_capture_tick = false;
	Sequence sequence = 0, last_presented = 0;
	std::atomic<std::uint64_t> tick{0}, submitted{0}, completed{0}, dropped{0}, repeated{0}, captures{0};
	std::atomic<ErrorCode> failure{ErrorCode::model_missing};
	std::atomic<bool> stopping{false}, reset_requested{false};
	std::thread worker;
	CallbackTimes timings, graphics_timings;
	FrameTiming frame_timing;
	std::array<Duration, FramePool::capacity> input_transfers{};
	TimePoint last_render{}, last_log = obs_now();
	TimePoint next_capture{};
	Error preparation_error;
	Generation resource_attempt = 0;
	std::uint64_t logged_count = 0;

	explicit Impl(obs_source_t *owner) : source(owner)
	{
		worker = std::thread([this] { run(); });
	}
	~Impl()
	{
		stopping = true;
		worker.join();
		if (target)
			obs_weak_source_release(target);
	}
	template<typename Function> void graphics_task(Function &&function)
	{
		struct Task {
			Function &function;
			CallbackTimes &timings;
			std::exception_ptr exception;
		} task{function, graphics_timings, {}};
		// OBS drains graphics tasks before its destroy queue at shutdown. Only
		// the worker waits here; the task never waits for model or GPU completion.
		obs_queue_task(
			OBS_TASK_GRAPHICS,
			[](void *parameter) {
				auto &task = *static_cast<Task *>(parameter);
				const auto start = obs_now();
				try {
					GraphicsScope graphics;
					task.function();
				} catch (...) {
					task.exception = std::current_exception();
				}
				task.timings.record(obs_now() - start, {});
			},
			&task, true);
		if (task.exception)
			std::rethrow_exception(task.exception);
	}
	void invalidate()
	{
		pool.advance_generation();
		frame_timing.reset();
		last_presented = 0;
		next_capture = {};
		failure = ErrorCode::busy;
	}
	void retire_presented()
	{
		if (!presented)
			return;
		const auto index = presented->input.lease.id().index;
		retired[index] = std::move(presented);
		presented.reset();
	}
	void collect()
	{
		if (!transport)
			return;
		transport->discard_stale();
		for (auto &result : retired)
			if (result && transport->read_finished(*result)) {
				result->input.lease.transition(FrameState::retired);
				result.reset();
			}
	}
	void log_timings()
	{
		const auto now = obs_now();
		const auto count = timings.count.load();
		if (now - last_log < std::chrono::seconds(10) || count == logged_count)
			return;
		Extent native;
		{
			std::lock_guard lock(mutex);
			native = size;
		}
		blog(LOG_INFO,
		     "[corridorkey:w03] instance=%llu native=%ux%u callbacks=%llu self_ms_p50<=%.2f self_ms_p95<=%.2f total_ms_p50<=%.2f total_ms_p95<=%.2f captures=%llu submitted=%llu completed=%llu dropped=%llu repeated=%llu status=%u graphics_tasks=%llu graphics_ms_p50<=%.2f graphics_ms_p95<=%.2f",
		     static_cast<unsigned long long>(instance), native.width, native.height,
		     static_cast<unsigned long long>(count), CallbackTimes::quantile(timings.self, count, .5),
		     CallbackTimes::quantile(timings.self, count, .95),
		     CallbackTimes::quantile(timings.total, count, .5),
		     CallbackTimes::quantile(timings.total, count, .95),
		     static_cast<unsigned long long>(captures.load()),
		     static_cast<unsigned long long>(submitted.load()),
		     static_cast<unsigned long long>(completed.load()), static_cast<unsigned long long>(dropped.load()),
		     static_cast<unsigned long long>(repeated.load()), static_cast<unsigned>(failure.load()),
		     static_cast<unsigned long long>(graphics_timings.count.load()),
		     CallbackTimes::quantile(graphics_timings.total, graphics_timings.count.load(), .5),
		     CallbackTimes::quantile(graphics_timings.total, graphics_timings.count.load(), .95));
		logged_count = count;
		last_log = now;
	}
	void work(Generation &prepared)
	{
		SettingsSnapshot snapshot;
		FakeOptions config;
		Generation generation;
		bool active;
		{
			std::lock_guard lock(mutex);
			generation = pool.generation();
			snapshot = settings;
			config = options;
			active = enabled && host_enabled && snapshot && !snapshot->context.bypass && size.width &&
				 size.height;
		}
		if (prepared != generation) {
			pipeline.cancel();
			scheduler.reset();
			pipeline.configure(config);
			PrepareRequest request;
			request.instance = instance;
			request.generation = generation;
			request.settings = snapshot;
			const auto problem = pipeline.prepare(request);
			preparation_error = problem;
			prepared = generation;
			if (problem)
				failure = problem.code;
		}
		if (!active && !transport) {
			log_timings();
			return;
		}
		pipeline.advance(obs_now());
		auto result = pipeline.poll_result();
		std::vector<std::uint8_t> rgba;
		bool result_failed = false;
		if (result) {
			scheduler.completed();
			++completed;
			if (!result->input.lease.current() || !active) {
				result.reset();
			} else if (result->failure) {
				failure = result->failure.code;
				result_failed = true;
				result.reset();
			} else if (presentable(*result, instance, generation, 0, obs_now(),
					       std::chrono::milliseconds(snapshot->context.stale_timeout_ms))) {
				const auto pack_start = obs_now();
				result->timings.input_transfer = input_transfers[result->input.lease.id().index];
				rgba = pack(*result);
				result->timings.output_transfer = obs_now() - pack_start;
			} else {
				failure = ErrorCode::timeout;
				result.reset();
			}
		}
		std::optional<InputFrame> input;
		const auto upload_start = obs_now();
		graphics_task([&] {
			std::lock_guard lock(mutex);
			if (presented && (!presented->input.lease.current() || !active || result_failed))
				retire_presented();
			if (result && result->input.lease.current() && transport && transport->upload(*result, rgba)) {
				result->timings.output_transfer += obs_now() - upload_start;
				retire_presented();
				result->input.lease.transition(FrameState::presenting);
				last_presented = result->identity.sequence;
				presented = std::move(result);
				failure = ErrorCode::none;
			}
			collect();
			if (active && settings && pool.in_use() == 0 && resource_attempt != pool.generation()) {
				resource_attempt = pool.generation();
				const auto grid = settings->quality.grid;
				if (size.width > 8192 || size.height > 8192 || !grid.width || !grid.height ||
				    static_cast<std::uint64_t>(grid.width) * grid.height > 1024 * 1024) {
					failure = ErrorCode::unsupported;
				} else if (!transport || !transport->available() || transport->native_size() != size ||
					   transport->grid_size() != grid) {
					transport.reset();
					transport = std::make_unique<StagedTransport>(source, size, grid);
					if (!transport->available())
						failure = ErrorCode::unsupported;
				}
			}
			if (active && transport && transport->available())
				input = transport->take_ready(tick.load());
			if (transport && transport->failure())
				failure = transport->failure().code;
		});
		if (input && input->lease.current()) {
			input->analysis = transport->analysis(*input);
			input_transfers[input->lease.id().index] = obs_now() - input->identity.captured_at;
			const auto previous_dropped = scheduler.dropped();
			scheduler.offer(std::move(*input));
			dropped += scheduler.dropped() - previous_dropped;
		}
		if (active && !preparation_error && generation == pool.generation()) {
			const auto problem = scheduler.submit(pipeline, tick.load(), obs_now());
			if (!problem)
				++submitted;
			else if (problem.code != ErrorCode::busy)
				failure = problem.code;
		}
		log_timings();
	}
	void run() noexcept
	{
		Generation prepared = 0;
		while (!stopping.load()) {
			try {
				work(prepared);
			} catch (const std::bad_alloc &) {
				failure = ErrorCode::out_of_memory;
			} catch (...) {
				failure = ErrorCode::internal;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		pipeline.cancel();
		scheduler.reset();
		pipeline.shutdown();
		pool.advance_generation();
		graphics_task([] { gs_flush(); });
		// Poll once per graphics task while the detached worker owns retirement.
		bool drained = false;
		while (!drained) {
			graphics_task([&] {
				retire_presented();
				collect();
				if (pool.in_use() == 0) {
					transport.reset();
					drained = true;
				}
			});
		}
	}
};
FilterInput::FilterInput(obs_source_t *source) : impl_(std::make_unique<Impl>(source)) {}
FilterInput::~FilterInput() = default;
void FilterInput::update(obs_data_t *data, SettingsSnapshot settings)
{
	std::lock_guard lock(impl_->mutex);
	impl_->settings = std::move(settings);
	impl_->enabled = obs_data_get_bool(data, "w03_fake_enabled");
	impl_->options.delay =
		std::chrono::milliseconds(std::clamp<long long>(obs_data_get_int(data, "w03_fake_delay"), 0, 2000));
	impl_->options.reorder_delay = obs_data_get_bool(data, "w03_fake_reorder") ? std::chrono::milliseconds(100)
										   : std::chrono::milliseconds(0);
	impl_->options.fault =
		static_cast<FakeFault>(std::clamp<long long>(obs_data_get_int(data, "w03_fake_fault"), 0, 3));
	impl_->invalidate();
}
void FilterInput::reset() noexcept
{
	impl_->reset_requested = true;
}
void FilterInput::tick() noexcept
{
	auto &s = *impl_;
	s.tick = obs_get_video_frame_time();
	std::unique_lock lock(s.mutex, std::try_to_lock);
	if (!lock.owns_lock())
		return;
	auto *target = obs_filter_get_target(s.source);
	const Extent size{target ? obs_source_get_base_width(target) : 0,
			  target ? obs_source_get_base_height(target) : 0};
	const bool enabled = obs_source_enabled(s.source);
	const bool changed = target ? !s.target || !obs_weak_source_references_source(s.target, target)
				    : s.target != nullptr;
	if (changed) {
		if (s.target)
			obs_weak_source_release(s.target);
		s.target = target ? obs_source_get_weak_source(target) : nullptr;
	}
	if (changed || size != s.size || enabled != s.host_enabled || s.reset_requested.exchange(false)) {
		s.size = size;
		s.host_enabled = enabled;
		s.invalidate();
		if (!size.width || !size.height)
			s.failure = ErrorCode::invalid_input;
	}
}
bool FilterInput::render() noexcept
{
	auto &s = *impl_;
	const auto start = obs_now();
	Duration host{};
	struct Record {
		Impl &s;
		TimePoint start;
		Duration &host;
		~Record() { s.timings.record(obs_now() - start, host); }
	} record{s, start, host};
	const bool keyed = [&]() noexcept {
		std::unique_lock lock(s.mutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			s.failure = ErrorCode::busy;
			return false;
		}
		if (!s.enabled || !s.host_enabled || !s.settings || s.settings->context.bypass) {
			s.failure = ErrorCode::model_missing;
			return false;
		}
		if (!s.size.width || !s.size.height) {
			s.failure = ErrorCode::invalid_input;
			return false;
		}
		const auto now = obs_now();
		auto *target = obs_filter_get_target(s.source);
		if (!target || !s.target || !obs_weak_source_references_source(s.target, target) ||
		    obs_source_get_base_width(target) != s.size.width ||
		    obs_source_get_base_height(target) != s.size.height || s.reset_requested.load()) {
			s.failure = ErrorCode::busy;
			return false;
		}
		const gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F};
		if (!target || obs_source_get_color_space(target, 2, spaces) != GS_CS_SRGB) {
			s.failure = ErrorCode::unsupported;
			return false;
		}
		if (s.last_render != TimePoint{} && now - s.last_render > std::chrono::milliseconds(500))
			s.invalidate();
		s.last_render = now;
		if (!s.transport || !s.transport->available() || s.transport->native_size() != s.size ||
		    s.transport->grid_size() != s.settings->quality.grid)
			return false;
		const auto tick = obs_get_video_frame_time();
		if (!s.has_capture_tick || s.last_capture_tick != tick) {
			s.has_capture_tick = true;
			s.last_capture_tick = tick;
			auto lease = now >= s.next_capture ? s.pool.try_acquire() : std::nullopt;
			if (lease) {
				InputFrame input;
				input.lease = std::move(*lease);
				input.identity = {s.instance, s.pool.generation(), ++s.sequence, tick, now};
				input.transform.source_size = s.size;
				input.transform.user_crop = input.transform.sampled_roi = s.settings->context.crop;
				input.settings = s.settings;
				const auto problem = s.transport->try_capture(input);
				host = s.transport->host_draw_time();
				if (problem) {
					++s.dropped;
					s.failure = problem.code;
					return false;
				}
				++s.captures;
				const auto fps = s.settings->quality.target_fps;
				s.next_capture = fps ? now + std::chrono::nanoseconds(1000000000 / fps) : now;
			} else {
				++s.dropped;
			}
		} else {
			++s.repeated;
		}
		if (!s.presented || !presentable(*s.presented, s.instance, s.pool.generation(), s.last_presented, now,
						 std::chrono::milliseconds(s.settings->context.stale_timeout_ms))) {
			if (s.presented)
				s.failure = ErrorCode::timeout;
			return false;
		}
		std::shared_ptr<const Completion> completion;
		const auto problem = s.transport->try_present(*s.presented, completion);
		if (problem)
			s.failure = problem.code;
		else {
			s.failure = ErrorCode::none;
			s.frame_timing.record(*s.presented, obs_now(), tick);
		}
		return !problem;
	}();
	if (!keyed) {
		const auto before_host = obs_now();
		const bool began = obs_source_process_filter_begin(s.source, GS_RGBA, OBS_NO_DIRECT_RENDERING);
		host += obs_now() - before_host;
		if (began)
			obs_source_process_filter_end(s.source, obs_get_base_effect(OBS_EFFECT_DEFAULT), 0, 0);
	}
	return true;
}
StatusSnapshot FilterInput::status() const
{
	const auto &s = *impl_;
	auto status = std::make_shared<Status>();
	status->backend = Backend::fake;
	status->transport = TransportKind::staged_grid;
	status->generation = s.pool.generation();
	status->failure = error(s.failure.load());
	status->lifecycle = status->failure ? Lifecycle::bypassed : Lifecycle::ready;
	status->diagnostic_detail = status->failure ? std::string(status->failure.action)
						    : "Fake key of filter input; no neural model loaded.";
	if (status->failure.code == ErrorCode::busy)
		status->diagnostic_detail = "Waiting for valid input or worker capacity; passthrough.";
	else if (status->failure.code == ErrorCode::invalid_input)
		status->diagnostic_detail = "No valid source frame; passthrough.";
	else if (status->failure.code == ErrorCode::timeout)
		status->diagnostic_detail = "Result exceeds the maximum frame age; passthrough.";
	const auto timing = s.frame_timing.snapshot(obs_now());
	if (timing.presentations && obs_now() - timing.last_presented < std::chrono::milliseconds(250)) {
		status->age_median_ms = timing.age_median_ms;
		status->age_p95_ms = timing.age_p95_ms;
		status->inference_median_ms = timing.inference_median_ms;
		status->input_transfer_median_ms = timing.input_transfer_median_ms;
		status->output_transfer_median_ms = timing.output_transfer_median_ms;
		status->queue_median_ms = timing.queue_median_ms;
		status->keyed_fps = timing.keyed_fps;
	}
	status->repeated = timing.repeated;
	status->submitted = s.submitted.load();
	status->completed = s.completed.load();
	status->dropped = s.dropped.load();
	return status;
}
void FilterInput::add_properties(obs_properties_t *properties)
{
	auto *group = obs_properties_create();
	obs_properties_add_bool(group, "w03_fake_enabled", "Key source with diagnostic fake");
	obs_properties_add_int(group, "w03_fake_delay", "Fake delay (ms)", 0, 2000, 1);
	obs_properties_add_bool(group, "w03_fake_reorder", "Extra delay on odd frames");
	auto *fault = obs_properties_add_list(group, "w03_fake_fault", "Injected failure", OBS_COMBO_TYPE_LIST,
					      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fault, "None", 0);
	obs_property_list_add_int(fault, "Out of memory", 1);
	obs_property_list_add_int(fault, "Backend unavailable", 2);
	obs_property_list_add_int(fault, "Invalid output", 3);
	obs_properties_add_group(properties, "w03_fake", "Input capture diagnostics (no neural model)",
				 OBS_GROUP_NORMAL, group);
}
} // namespace corridorkey
