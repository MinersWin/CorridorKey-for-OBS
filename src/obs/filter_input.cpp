// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "filter_input.hpp"
#include "staged_transport.hpp"
#include "engine/fake_pipeline.hpp"
#ifdef CORRIDORKEY_CK2_RUNTIME
#include "engine/ck2_ort.hpp"
#include "models/installation.hpp"
#include "models/activation.hpp"
#include "ui/manager_model.hpp"
#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif
#include "util/utf8_path.hpp"
#include <cstdlib>
#endif
#include "runtime/scheduler.hpp"
#include "telemetry/frame_timing.hpp"
#include "telemetry/obs_clock.hpp"
#include "telemetry/diagnostics.hpp"
#include "runtime/capture_pacer.hpp"
#include <obs-module.h>
#include <util/bmem.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <limits>
#include <exception>
#include <condition_variable>

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
std::vector<float> pack(const FrameResult &result)
{
	const auto rgb = result.foreground.storage->host_floats();
	const auto alpha = result.alpha.storage->host_floats();
	std::vector<float> rgba(alpha.size() * 4);
	for (std::size_t p = 0; p < alpha.size(); ++p) {
		for (unsigned c = 0; c < 3; ++c)
			rgba[p * 4 + c] = rgb[p * 3 + c];
		rgba[p * 4 + 3] = alpha[p];
	}
	return rgba;
}
} // namespace
struct FilterInput::Impl {
	obs_source_t *source;
	const InstanceId instance = next_instance.fetch_add(1);
	mutable std::mutex mutex;
	std::mutex mailbox_mutex;
	FramePool pool{FramePool::maximum_capacity};
	FakePipeline fake{{}, FakeExecution::manual};
	Pipeline *pipeline = &fake;
#ifdef CORRIDORKEY_CK2_RUNTIME
	std::unique_ptr<Ck2Pipeline> real;
	std::shared_ptr<const ReadyModel> leased_model, leased_refiner;
	std::string prepared_key;
	std::vector<std::shared_ptr<refiner::Stage>> retained_refiners;
	std::vector<models::Manifest> manifests;
	TimePoint next_model_check{};
	std::atomic<bool> model_pending{false}, preparing_model{false};
#endif
	std::stop_source preparation;
	Crop capture_roi, completed_roi;
	std::string model_detail;
	Backend model_backend = Backend::fake;
	Scheduler scheduler{admission}; // Worker only; render deduplication is separate.
	SettingsSnapshot settings;
	FakeOptions options;
	std::unique_ptr<StagedTransport> transport;
	nvidia::TextureTransfers device_transfers;
	std::atomic<TransportKind> chosen_transport{TransportKind::staged_grid};
	std::optional<FrameResult> presented;
	std::optional<InputFrame> staged_input;
	std::optional<FrameResult> completed_result;
	std::vector<float> completed_pixels;
	std::optional<FrameResult> publishing_result;
	std::vector<float> publishing_pixels;
	std::shared_ptr<refiner::Stage> completed_refiner, publishing_refiner;
	Crop publishing_roi;
	TimePoint publishing_started{};
	TimePoint publication_started{};
	std::atomic<bool> transfer_queued{false}, input_available{false};
	SettingsSnapshot worker_settings;
	FakeOptions worker_options;
	bool worker_active = false, diagnostic = false, worker_diagnostic = false;
	std::uint64_t transfer_tick = 0;
	bool has_transfer_tick = false;
	std::atomic<bool> discard_presentation{false};
	std::array<std::optional<FrameResult>, FramePool::maximum_capacity> retired;
	Extent size;
	gs_color_space color_space = GS_CS_SRGB;
	obs_weak_source_t *target = nullptr;
	bool enabled = false, host_enabled = true;
	int debug_view = 0;
	std::atomic<bool> unsupported_hdr{false};
	std::uint64_t last_capture_tick = 0;
	bool has_capture_tick = false;
	Sequence sequence = 0, last_presented = 0;
	std::atomic<std::uint64_t> tick{0}, submitted{0}, completed{0}, dropped{0}, repeated{0}, captures{0};
	std::atomic<ErrorCode> failure{ErrorCode::model_missing};
	std::atomic<bool> stopping{false}, reset_requested{false};
	std::thread worker;
	std::mutex wake_mutex;
	std::condition_variable wake;
	CallbackTimes timings, graphics_timings, post_timings, tick_transfer_timings, inference_cpu_timings,
		worker_timings;
	FrameTiming frame_timing;
	std::array<Duration, FramePool::maximum_capacity> input_transfers{};
	TimePoint last_render{}, last_log = obs_now();
	CapturePacer pacer;
	Error preparation_error;
	Generation resource_attempt = 0;
	std::uint64_t logged_count = 0;
	std::array<double, 4> stage_total{};
	std::uint64_t model_frames = 0, moge_runs = 0, last_published = 0;
	std::atomic<std::uint64_t> published{0};
	double inference_total = 0;

	explicit Impl(obs_source_t *owner) : source(owner)
	{
		worker = std::thread([this] { run(); });
	}
	~Impl()
	{
		stopping = true;
		wake.notify_one();
		{
			std::lock_guard lock(mutex);
			preparation.request_stop();
		}
		worker.join();
#ifdef CORRIDORKEY_CK2_RUNTIME
		ui::preparation_finished(instance);
#endif
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
		preparation.request_stop();
		capture_roi = settings ? settings->context.crop : Crop{};
		pool.advance_generation();
		frame_timing.reset();
		last_presented = 0;
		pacer.reset();
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
		const auto age = frame_timing.snapshot(now);
		blog(LOG_INFO,
		     "[corridorkey:w08] age_median_ms=%.3f age_p95_ms=%.3f input_ms=%.3f output_ms=%.3f inference_ms=%.3f queue_ms=%.3f keyed_fps=%.3f",
		     age.age_median_ms.value_or(-1), age.age_p95_ms.value_or(-1),
		     age.input_transfer_median_ms.value_or(-1), age.output_transfer_median_ms.value_or(-1),
		     age.inference_median_ms.value_or(-1), age.queue_median_ms.value_or(-1),
		     age.keyed_fps.value_or(-1));
		blog(LOG_INFO, "[corridorkey:w08] post_cpu_ms_p50<=%.2f post_cpu_ms_p95<=%.2f",
		     CallbackTimes::quantile(post_timings.total, post_timings.count.load(), .5),
		     CallbackTimes::quantile(post_timings.total, post_timings.count.load(), .95));
		blog(LOG_INFO, "[corridorkey:w08] tick_transfer_cpu_ms_p50<=%.2f tick_transfer_cpu_ms_p95<=%.2f",
		     CallbackTimes::quantile(tick_transfer_timings.total, tick_transfer_timings.count.load(), .5),
		     CallbackTimes::quantile(tick_transfer_timings.total, tick_transfer_timings.count.load(), .95));
		blog(LOG_INFO,
		     "[corridorkey:w08] fake_cpu_ms_p50<=%.2f fake_cpu_ms_p95<=%.2f worker_cpu_ms_p50<=%.2f worker_cpu_ms_p95<=%.2f",
		     CallbackTimes::quantile(inference_cpu_timings.total, inference_cpu_timings.count.load(), .5),
		     CallbackTimes::quantile(inference_cpu_timings.total, inference_cpu_timings.count.load(), .95),
		     CallbackTimes::quantile(worker_timings.total, worker_timings.count.load(), .5),
		     CallbackTimes::quantile(worker_timings.total, worker_timings.count.load(), .95));
		if (model_frames) {
			blog(LOG_INFO,
			     "[corridorkey:w09] frames=%llu published=%llu keyed_fps=%.2f moge_ms=%.3f rvm_ms=%.3f stage1_ms=%.3f stage2_ms=%.3f worker_ms=%.3f",
			     static_cast<unsigned long long>(model_frames),
			     static_cast<unsigned long long>(published.load()),
			     static_cast<double>(published - last_published) /
				     std::chrono::duration<double>(now - last_log).count(),
			     moge_runs ? stage_total[0] / static_cast<double>(moge_runs) : 0,
			     stage_total[1] / static_cast<double>(model_frames),
			     stage_total[2] / static_cast<double>(model_frames),
			     stage_total[3] / static_cast<double>(model_frames),
			     inference_total / static_cast<double>(model_frames));
			stage_total = {};
			model_frames = moge_runs = 0;
			inference_total = 0;
		}
		last_published = published.load();
		logged_count = count;
		last_log = now;
	}
	bool active() const
	{
		return !stopping && enabled && host_enabled && settings && !settings->context.bypass && size.width &&
		       size.height;
	}
	// At most one queued graphics callback; the worker continues polling inference.
	void queue_transfer()
	{
		const auto current_tick = tick.load();
		if ((has_transfer_tick && transfer_tick == current_tick) || transfer_queued.exchange(true))
			return;
		has_transfer_tick = true;
		transfer_tick = current_tick;
		obs_queue_task(
			OBS_TASK_GRAPHICS,
			[](void *parameter) {
				auto &s = *static_cast<Impl *>(parameter);
				const auto start = obs_now();
				try {
					GraphicsScope graphics;
					std::lock_guard lock(s.mutex);
					s.transfer();
				} catch (const std::bad_alloc &) {
					s.failure = ErrorCode::out_of_memory;
				} catch (...) {
					s.failure = ErrorCode::internal;
				}
				s.graphics_timings.record(obs_now() - start, {});
				s.transfer_queued = false;
			},
			this, false);
	}
	void transfer()
	{
		if (presented &&
		    (!presented->input.lease.current() || !active() || discard_presentation.exchange(false)))
			retire_presented();
		{
			std::unique_lock mailbox(mailbox_mutex, std::try_to_lock);
			if (mailbox.owns_lock()) {
				if (staged_input && (!staged_input->lease.current() || !active()))
					staged_input.reset();
				if (completed_result && (!completed_result->input.lease.current() || !active())) {
					completed_result.reset();
					completed_pixels.clear();
				}
			}
		}
		collect();
		if (active() && pool.in_use() == 0 && resource_attempt != pool.generation()) {
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
		poll_input();
	}
	void poll_input()
	{
		std::unique_lock mailbox(mailbox_mutex, std::try_to_lock);
		if (!mailbox.owns_lock())
			return;
		if (active() && transport && transport->available() && !staged_input) {
			const auto previous = transport->discarded();
			staged_input = transport->take_ready();
			dropped += transport->discarded() - previous;
			input_available = staged_input.has_value();
			if (input_available)
				wake.notify_one();
		}
		if (transport && transport->failure())
			failure = transport->failure().code;
	}

	// The video tick precedes drawing. Submit ready post passes here so this
	// canvas frame can display the result without another graphics-task round trip.
	void publish()
	{
		std::unique_lock mailbox(mailbox_mutex, std::try_to_lock);
		if (!mailbox.owns_lock())
			return;
		if (!publishing_result) {
			if (!completed_result)
				return;
			publishing_result = std::move(completed_result);
			completed_result.reset();
			publishing_pixels.swap(completed_pixels);
			publishing_refiner = std::move(completed_refiner);
			publishing_roi = completed_roi;
			publishing_started = publication_started;
		}
		auto &result = publishing_result;
		const bool stale = !active() || !transport ||
				   !presentable(*result, instance, pool.generation(), last_presented, obs_now(),
						std::chrono::milliseconds(settings->context.stale_timeout_ms));
		if (stale && (!transport || !transport->refining())) {
			result.reset();
			return;
		}
		const auto start = obs_now();
		GraphicsScope graphics;
		const bool uploaded = transport->upload(*result, publishing_pixels, debug_view, publishing_refiner);
		post_timings.record(obs_now() - start, {});
		if (transport->refining())
			return;
		result->timings.output_transfer += obs_now() - publishing_started;
		if (!uploaded || stale) {
			if (!uploaded)
				failure = ErrorCode::device_lost;
			const auto index = result->input.lease.id().index;
			retired[index] = std::move(result);
			result.reset();
			return;
		}
		retire_presented();
		result->input.lease.transition(FrameState::presenting);
		last_presented = result->identity.sequence;
		presented = std::move(result);
		result.reset();
		capture_roi = publishing_roi;
		++published;
		failure = ErrorCode::none;
	}
	void work(Generation &prepared)
	{
#ifdef CORRIDORKEY_CK2_RUNTIME
		// The last session reference is released here, so graphics never joins an inference worker.
		std::erase_if(retained_refiners, [](const auto &stage) { return stage.use_count() == 1; });
#endif
		std::stop_token stop;
		const auto work_start = obs_now();
#ifdef CORRIDORKEY_CK2_RUNTIME
		if (model_pending && Clock::now() >= next_model_check) {
			next_model_check = Clock::now() + std::chrono::seconds(1);
			SettingsSnapshot current;
			{
				std::lock_guard lock(mutex);
				current = settings;
			}
			if (current) {
				const auto *manifest = models::activation_manifest(manifests, *current);
				const auto *refiner = models::activation_refiner_manifest(manifests, *current);
				if ((!current->quality.refiner_enabled ||
				     (refiner &&
				      models::fs::is_regular_file(
					      models::Cache(models::obs_model_cache_path()).revision(refiner->key) /
					      "ready.json"))) &&
				    manifest &&
				    models::fs::is_regular_file(
					    models::Cache(models::obs_model_cache_path()).revision(manifest->key) /
					    "ready.json")) {
					std::lock_guard lock(mutex);
					model_pending = false;
					invalidate();
				}
			}
		}
#endif
		const auto generation = pool.generation();
		if (prepared != generation) {
			std::lock_guard lock(mutex);
			preparation = std::stop_source{};
			stop = preparation.get_token();
			worker_settings = settings;
			worker_options = options;
			worker_diagnostic = diagnostic;
			worker_active = active();
		}
		const auto &snapshot = worker_settings;
		const auto &config = worker_options;
		const bool active = worker_active;
		if (prepared != generation) {
			pipeline->cancel();
			scheduler.reset();
			PrepareRequest request;
			request.instance = instance;
			request.generation = generation;
			request.settings = snapshot;
			request.cancellation = stop;
			Error problem;
#ifdef CORRIDORKEY_CK2_RUNTIME
			if (snapshot && active && !worker_diagnostic) {
				model_pending = false;
				preparing_model = true;
				{
					std::lock_guard lock(mutex);
					model_detail = "Preparing CorridorKey; video stays in passthrough.";
				}
				struct Finish {
					Impl &self;
					~Finish()
					{
						self.preparing_model = false;
						ui::preparation_finished(self.instance);
					}
				} finish{*this};
				try {
					ck2::RuntimeConfig runtime;
					std::shared_ptr<const ReadyModel> model;
#ifdef CORRIDORKEY_CK2_DEV
					const char *override_path = std::getenv("CORRIDORKEY_CK2_DEV_CONFIG");
					if (override_path && *override_path) {
						auto config = ck2::read_development_config(utf8_path(override_path));
						runtime = config.runtime;
						model = ck2::development_model(
							config.models, snapshot->quality.grid,
							snapshot->quality.profile == Profile::quality,
							config.release_fixture,
							snapshot->quality.refiner_enabled
								? snapshot->quality.refiner_max_tiles
								: 0);
						request.transport.adapter = config.adapter;
					} else
#endif
					{
						if (manifests.empty())
							manifests = models::installed_catalogue();
						const auto *manifest =
							models::activation_manifest(manifests, *snapshot);
						models::require(
							manifest != nullptr,
							"No package supports this CK2 grid/preset. Open Manage models to install a supported package.",
							ErrorCode::model_missing);
						models::Cache cache(models::obs_model_cache_path());
						auto licences =
							models::LicenceRegistry::parse(models::read_small_file([] {
								char *raw = obs_module_file("models/licenses-v1.json");
								std::unique_ptr<char, decltype(&bfree)> owned(raw,
													      bfree);
								models::require(raw != nullptr,
										"Model licences are missing");
								return utf8_path(raw);
							}()));
						models::require(
							models::AcceptanceStore(cache).contains(*manifest, licences),
							"Open Manage models, then Set up CorridorKey and accept the model licences.",
							ErrorCode::model_missing);
						// A held reader lease keeps this verified revision immutable across resets.
						// Rehashing gigabytes on every source/settings generation starves live activation.
						if (leased_model && leased_model->key == manifest->key &&
						    leased_model->manifest_sha256 == manifest->digest)
							model = leased_model;
						else
							model = cache.acquire(*manifest, stop);
						models::require(
							bool(model),
							"This grid's model package is not installed. Open Manage models to install it.",
							ErrorCode::model_missing);
						leased_model = model;
						if (snapshot->quality.refiner_enabled) {
							const auto *refiner = models::activation_refiner_manifest(
								manifests, *snapshot);
							models::require(
								refiner != nullptr,
								"No edge-refiner package supports this tile budget. Open Manage models to install a supported package.",
								ErrorCode::model_missing);
							models::require(
								models::AcceptanceStore(cache).contains(*refiner,
													licences),
								"Open Manage models and accept the edge-refiner licence.",
								ErrorCode::model_missing);
							auto refinement =
								leased_refiner && leased_refiner->key == refiner->key &&
										leased_refiner->manifest_sha256 ==
											refiner->digest
									? leased_refiner
									: cache.acquire(*refiner, stop);
							models::require(
								bool(refinement),
								"The edge-refiner package is not installed. Open Manage models to install it.",
								ErrorCode::model_missing);
							leased_refiner = refinement;
							model = models::activation_model(std::move(model),
											 std::move(refinement));
						}
						runtime.probe = models::installed_probe_config();
						runtime.compiler.helper = runtime.probe.helper;
						runtime.compiler.scratch = runtime.probe.scratch;
						runtime.compiler.timeout = std::chrono::minutes(15);
						runtime.cache = cache.path("engines");
						graphics_task([&] {
#ifdef _WIN32
							auto *device = static_cast<ID3D11Device *>(gs_get_device_obj());
							Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
							Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
							DXGI_ADAPTER_DESC desc{};
							if (device &&
							    SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
							    SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
							    SUCCEEDED(adapter->GetDesc(&desc))) {
								char luid[40]{};
								std::snprintf(luid, sizeof(luid), "luid:%08x:%08x",
									      static_cast<unsigned>(
										      desc.AdapterLuid.HighPart),
									      static_cast<unsigned>(
										      desc.AdapterLuid.LowPart));
								request.transport.adapter.stable_id = luid;
								request.transport.adapter.vendor_id = desc.VendorId;
								request.transport.adapter.device_id = desc.DeviceId;
							}
#endif
						});
					}
					request.model = model;
					const auto key =
						model->manifest_sha256 + ":" +
						std::to_string(snapshot->quality.grid.width) + ":" +
						std::to_string(snapshot->quality.grid.height) + ":" +
						std::to_string(static_cast<int>(snapshot->quality.profile)) + ":" +
						std::to_string(static_cast<int>(snapshot->backend.backend)) + ":" +
						std::to_string(static_cast<int>(snapshot->backend.transport)) + ":" +
						std::to_string(snapshot->quality.refiner_enabled) + ":" +
						std::to_string(snapshot->quality.refiner_max_tiles) + ":" +
						snapshot->quality.model_revision;
					runtime.info = [this](const std::string &message) {
						blog(LOG_INFO, "[corridorkey] %s",
						     diagnostics::redact(message).c_str());
					};
					runtime.record_probe = diagnostics::record_probe;
					runtime.compiler.finished = [id = instance] {
						ui::preparation_finished(id);
					};
					runtime.compiler.state = [id = instance, key = model->key,
								  cancellation = preparation](Backend,
											      ModelState state) {
						ui::preparation_state(id, key, state, cancellation);
					};
					ui::preparation_state(instance, model->key, ModelState::warming, preparation);
					if (real && key == prepared_key &&
					    (!snapshot->quality.refiner_enabled || real->refiner_stage())) {
						pipeline = real.get();
						problem = real->reset(generation, ResetReason::explicit_reset);
					} else {
						auto replacement = std::make_unique<Ck2Pipeline>(
							[runtime](const PrepareRequest &r) {
								return ck2::prepare_ort(r, runtime);
							},
							obs_now);
						problem = replacement->prepare(request);
						{
							std::lock_guard lock(mutex);
							model_detail = replacement->detail();
						}
						if (!problem && !stop.stop_requested()) {
							// Selection is committed only after preparation succeeds; the pipeline owns its reader lease.
							const auto *manifest =
								models::activation_manifest(manifests, *snapshot);
							if (manifest)
								models::Cache(models::obs_model_cache_path())
									.select_current(*manifest);
							if (const auto *refiner = models::activation_refiner_manifest(
								    manifests, *snapshot))
								models::Cache(models::obs_model_cache_path())
									.select_current(*refiner);
							if (auto stage = replacement->refiner_stage())
								retained_refiners.push_back(std::move(stage));
							real = std::move(replacement);
							pipeline = real.get();
							prepared_key = key;
						}
					}
					{
						std::lock_guard lock(mutex);
						if (real && !problem) {
							model_backend = real->capabilities().backend;
							model_detail = real->detail();
						}
					}
				} catch (const models::Failure &f) {
					problem = error(f.code);
					model_pending = f.code == ErrorCode::model_missing;
					std::lock_guard lock(mutex);
					model_detail = f.what();
				} catch (const std::exception &e) {
					problem = error(ErrorCode::backend_unavailable);
					std::lock_guard lock(mutex);
					model_detail = diagnostics::redact(e.what());
				}
				if (stop.stop_requested())
					problem = error(ErrorCode::cancelled);
			} else
#endif
			{
				pipeline = &fake;
				fake.configure(config);
				problem = fake.prepare(request);
			}
			preparation_error = problem;
			prepared = generation;
			if (problem) {
				failure = problem.code;
				blog(LOG_WARNING, "[corridorkey] Preparation failed: %s", model_detail.c_str());
			}
		}

		const auto inference_start = obs_now();
		if (pipeline == &fake)
			fake.advance(obs_now());
		auto result = pipeline->poll_result();
		const bool produced = result.has_value();
		if (produced)
			inference_cpu_timings.record(obs_now() - inference_start, {});
		std::vector<float> rgba;

		if (result) {
			scheduler.completed();
			++completed;
			if (!result->input.lease.current() || !active) {
				result.reset();
			} else if (result->failure) {
				failure = result->failure.code;
				discard_presentation = true;
				result.reset();
			} else if (presentable(*result, instance, generation, 0, obs_now(),
					       std::chrono::milliseconds(snapshot->context.stale_timeout_ms))) {
				const auto pack_start = obs_now();
				result->timings.input_transfer = input_transfers[result->input.lease.id().index];
				if (!transport->upload_device(*result, device_transfers)) {
					for (auto *image : {&result->alpha, &result->foreground})
						if (auto device = std::dynamic_pointer_cast<const ck2::DeviceImage>(
							    image->storage))
							*image = ck2::packed_image(ck2::download_tensor(device->tensor),
										   image->format);
					rgba = pack(*result);
				}
				transport->release_analysis(*result);
				result->timings.output_transfer = obs_now() - pack_start;
			} else {
				failure = ErrorCode::timeout;
				result.reset();
			}
		}
		Crop next_roi = snapshot ? snapshot->context.crop : Crop{};
#ifdef CORRIDORKEY_CK2_RUNTIME
		if (result && real && pipeline == real.get()) {
			next_roi = real->suggested_roi();
			const auto times = real->stage_times();
			++model_frames;
			moge_runs += times.moge_refreshed ? 1 : 0;
			stage_total[0] += times.moge_ms;
			stage_total[1] += times.rvm_ms;
			stage_total[2] += times.stage1_ms;
			stage_total[3] += times.stage2_ms;
			inference_total += std::chrono::duration<double, std::milli>(result->timings.inference).count();
		}
#endif
		std::optional<InputFrame> input;
		if (result || input_available) {
			std::lock_guard lock(mailbox_mutex);
			if (result && result->input.lease.current() && active) {
				completed_result.swap(result);
				completed_pixels.swap(rgba);
				publication_started = obs_now();
				completed_roi = next_roi;
#ifdef CORRIDORKEY_CK2_RUNTIME
				completed_refiner = real && pipeline == real.get() ? real->refiner_stage() : nullptr;
#endif
			}
			input = std::move(staged_input);
			staged_input.reset();
			input_available = false;
		}
		queue_transfer();
		if (input && input->lease.current()) {
			std::shared_ptr<nvidia::Context> context;
#ifdef CORRIDORKEY_CK2_RUNTIME
			if (real && pipeline == real.get())
				context = real->device_context();
#endif
			input->analysis = transport->analysis(*input, &device_transfers, std::move(context));
			const auto selected_transport = transport->transport_kind();
			if (chosen_transport.exchange(selected_transport) != selected_transport)
				blog(LOG_INFO, "[corridorkey:w10] transport=%s",
				     selected_transport == TransportKind::interop ? "d3d11-cuda" : "staged");
			input_transfers[input->lease.id().index] = obs_now() - input->identity.captured_at;
			const auto previous_dropped = scheduler.dropped();
			scheduler.offer(std::move(*input));
			dropped += scheduler.dropped() - previous_dropped;
		}
		if (active && !preparation_error && generation == pool.generation()) {
			const auto problem = scheduler.submit(*pipeline, tick.load(), obs_now());
			if (!problem)
				++submitted;
			else if (problem.code != ErrorCode::busy)
				failure = problem.code;
		}
		if (input || produced)
			worker_timings.record(obs_now() - work_start, {});
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
			std::unique_lock wait_lock(wake_mutex);
			wake.wait_for(wait_lock, std::chrono::milliseconds(1));
		}
		pipeline->cancel();
		scheduler.reset();
		pipeline->shutdown();
		device_transfers.clear();
		pool.advance_generation();
		graphics_task([&] {
			// FIFO barrier: the last asynchronous callback has returned before
			// its mailbox and graphics resources can be released.
			std::scoped_lock lock(mutex, mailbox_mutex);
			staged_input.reset();
			completed_result.reset();
			completed_pixels.clear();
			gs_flush();
		});
		// Poll once per graphics task while the detached worker owns retirement.
		bool drained = false;
		while (!drained) {
			graphics_task([&] {
				if (publishing_result) {
					if (transport && transport->refining())
						transport->upload(*publishing_result, publishing_pixels, debug_view,
								  publishing_refiner);
					if (!transport || !transport->refining()) {
						const auto index = publishing_result->input.lease.id().index;
						retired[index] = std::move(publishing_result);
						publishing_result.reset();
					}
				}
				retire_presented();
				collect();
				if (pool.in_use() == 0) {
					transport.reset();
					publishing_refiner.reset();
					completed_refiner.reset();
					drained = true;
				}
			});
		}
#ifdef CORRIDORKEY_CK2_RUNTIME
		retained_refiners.clear();
#endif
	}
};
FilterInput::FilterInput(obs_source_t *source) : impl_(std::make_unique<Impl>(source)) {}
FilterInput::~FilterInput() = default;
void FilterInput::update(obs_data_t *data, SettingsSnapshot settings)
{
	std::lock_guard lock(impl_->mutex);
	impl_->settings = std::move(settings);
	impl_->diagnostic = false;
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
	impl_->diagnostic = obs_data_get_bool(data, "w03_fake_enabled");
#endif
	impl_->enabled = impl_->diagnostic;
	impl_->debug_view = static_cast<int>(std::clamp<long long>(obs_data_get_int(data, "w08_debug_view"), 0, 7));
#ifdef CORRIDORKEY_CK2_RUNTIME
	impl_->enabled = true;
	impl_->model_pending = false;
#endif
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
	const gs_color_space preferred[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED, GS_CS_709_SCRGB};
	const auto color_space = target ? obs_source_get_color_space(target, 4, preferred) : GS_CS_SRGB;
	const bool changed = target ? !s.target || !obs_weak_source_references_source(s.target, target)
				    : s.target != nullptr;
	if (changed) {
		if (s.target)
			obs_weak_source_release(s.target);
		s.target = target ? obs_source_get_weak_source(target) : nullptr;
	}
	if (changed || size != s.size || color_space != s.color_space || enabled != s.host_enabled ||
	    s.reset_requested.exchange(false)) {
		s.size = size;
		s.color_space = color_space;
		s.host_enabled = enabled;
		s.invalidate();
		if (!size.width || !size.height)
			s.failure = ErrorCode::invalid_input;
	}
	try {
		s.publish();
		const auto start = obs_now();
		GraphicsScope graphics;
		s.poll_input();
		s.tick_transfer_timings.record(obs_now() - start, {});
	} catch (...) {
		s.failure = ErrorCode::internal;
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
#ifdef CORRIDORKEY_CK2_RUNTIME
		if (s.preparing_model || s.model_pending) {
			// Passthrough still receives frames. Preparation must not manufacture a source gap.
			s.last_render = obs_now();
			s.failure = s.model_pending ? ErrorCode::model_missing : ErrorCode::busy;
			return false;
		}
#endif
		const auto now = obs_now();
		auto *target = obs_filter_get_target(s.source);
		if (!target || !s.target || !obs_weak_source_references_source(s.target, target) ||
		    obs_source_get_base_width(target) != s.size.width ||
		    obs_source_get_base_height(target) != s.size.height || s.reset_requested.load()) {
			s.failure = ErrorCode::busy;
			return false;
		}
		const gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED, GS_CS_709_SCRGB};
		const auto color_space = obs_source_get_color_space(target, 4, spaces);
		s.unsupported_hdr = color_space != GS_CS_SRGB && color_space != GS_CS_SRGB_16F;
		if (s.unsupported_hdr) {
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
			auto lease = s.pacer.due(tick) ? s.pool.try_acquire() : std::nullopt;
			if (lease) {
				InputFrame input;
				input.lease = std::move(*lease);
				input.identity = {s.instance, s.pool.generation(), ++s.sequence, tick, now};
				input.transform.source_size = s.size;
				input.transform.user_crop = s.settings->context.crop;
				input.transform.sampled_roi = s.capture_roi;
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
				s.pacer.captured(tick, fps);
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
		if (s.unsupported_hdr) {
			obs_source_skip_video_filter(s.source);
			return true;
		}
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
	std::lock_guard lock(s.mutex);
	status->backend = s.model_backend;
	status->transport = s.chosen_transport.load();
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
	if (s.unsupported_hdr)
		status->diagnostic_detail = "HDR input is not supported by this model; passthrough.";
	if (!s.model_detail.empty())
		status->diagnostic_detail =
			s.model_detail +
			(status->failure ? " Passthrough: " + std::string(status->failure.action) : "");
	status->submitted = s.submitted.load();
	if (!s.model_detail.empty())
		status->diagnostic_detail += status->transport == TransportKind::interop
						     ? " D3D11/CUDA device transport."
						     : " Staged CPU transport.";
	status->completed = s.completed.load();
	status->dropped = s.dropped.load();
	return status;
}
void FilterInput::add_properties(obs_properties_t *properties)
{
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
	auto *group = obs_properties_create();
	obs_properties_add_bool(group, "w03_fake_enabled", "Key source with diagnostic fake");
	auto *view = obs_properties_add_list(group, "w08_debug_view", "Reconstruction view", OBS_COMBO_TYPE_LIST,
					     OBS_COMBO_FORMAT_INT);
	const char *views[] = {"Output", "Alpha",      "Foreground", "Coefficients",
			       "Band",   "Crop / ROI", "Input",      "Background fill"};
	for (int i = 0; i < 8; ++i)
		obs_property_list_add_int(view, views[i], i);
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
#else
	(void)properties;
#endif
}
} // namespace corridorkey
