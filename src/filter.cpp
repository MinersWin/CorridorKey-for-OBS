// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "filter.hpp"
#include "obs/filter_control.hpp"
#include "obs/properties.hpp"
#include "obs/audio_sync_obs.hpp"
#include "settings/obs_settings.hpp"
#include "telemetry/diagnostics.hpp"
#ifdef CORRIDORKEY_CK2_RUNTIME
#include "obs/ck1_runtime.hpp"
#endif
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
#include "obs/filter_input.hpp"
#endif
#include <obs-module.h>
#include <mutex>

namespace corridorkey {
namespace {
struct Filter final : FilterControl {
	explicit Filter(obs_source_t *owner)
		: source(owner)
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
		  ,
		  input(owner)
#endif
	{
		sync = std::make_unique<AudioSyncRuntime>(
			source, [this] { return settings_snapshot(); }, [this] { return pipeline_status(); });
	}
	obs_source_t *source;
	std::unique_ptr<AudioSyncRuntime> sync;
	obs_source_t *property_source() const override { return source; }
	mutable std::mutex control;
	SettingsSnapshot settings;
	SettingsSnapshot settings_snapshot() const override
	{
		std::lock_guard lock(control);
		return settings;
	}
	StatusSnapshot pipeline_status() const
	{
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
		auto status = input.status();
		diagnostics::record_error(status->failure.code);
		return status;
#else
		auto status = std::make_shared<Status>();
		status->lifecycle = Lifecycle::bypassed;
		status->failure = error(ErrorCode::model_missing);
		diagnostics::record_error(status->failure.code);
		return status;
#endif
	}
	StatusSnapshot status_snapshot() const override
	{
		auto status = std::make_shared<Status>(*pipeline_status());
		const auto value = sync->snapshot();
		const char *states[] = {"off", "waiting", "active", "shared", "manual", "missing", "paused"};
		status->audio_sync_state = states[static_cast<unsigned>(value.state)];
		if (value.applied_ns)
			status->audio_sync_offset_ms = static_cast<double>(*value.applied_ns) / 1000000.0;
		if (value.baseline_ns)
			status->audio_sync_baseline_ms = static_cast<double>(*value.baseline_ns) / 1000000.0;
		return status;
	}
	void request_reset() noexcept override
	{
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
		input.reset();
#endif
	}
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
	FilterInput input;
#endif
};
const char *filter_name(void *)
{
	return obs_module_text("Filter.Name");
}
void update_filter(void *data, obs_data_t *settings)
{
	try {
		auto *filter = static_cast<Filter *>(data);
		auto snapshot = read_settings(settings);
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
		filter->input.update(settings, snapshot);
#endif
		{
			std::lock_guard lock(filter->control);
			filter->settings = std::move(snapshot);
		}
		filter->sync->tick(true);
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] settings update failed; retaining previous settings");
	}
}
void enable_filter(void *data, calldata_t *)
{
	static_cast<Filter *>(data)->request_reset();
	static_cast<Filter *>(data)->sync->tick(true);
}
void *create_filter(obs_data_t *settings, obs_source_t *source)
{
	try {
		auto filter = std::make_unique<Filter>(source);
		update_filter(filter.get(), settings);
		diagnostics::register_instance(filter.get(), [value = filter.get()] {
			auto *target = obs_filter_get_target(value->source);
			return diagnostics::Instance{value->settings_snapshot(),
						     value->status_snapshot(),
						     {target ? obs_source_get_base_width(target) : 0,
						      target ? obs_source_get_base_height(target) : 0}};
		});
		signal_handler_connect(obs_source_get_signal_handler(source), "enable", enable_filter, filter.get());
		return filter.release();
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] filter creation failed");
		return nullptr;
	}
}
void destroy_filter(void *data)
{
	auto *filter = static_cast<Filter *>(data);
	diagnostics::unregister_instance(filter);
	filter->sync->stop();
	signal_handler_disconnect(obs_source_get_signal_handler(filter->source), "enable", enable_filter, filter);
	// OBS_TASK_DESTROY is present at the 30.2 floor. It also handles callers
	// releasing a source from a graphics callback without joining that thread.
	obs_queue_task(OBS_TASK_DESTROY, [](void *value) { delete static_cast<Filter *>(value); }, data, false);
}
void filter_defaults(obs_data_t *settings)
{
	try {
		set_filter_defaults(settings);
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] could not set defaults");
	}
}
obs_properties_t *filter_properties(void *data)
{
	try {
		PropertyCapabilities capabilities;
		capabilities.audio_sync = true;
#ifdef CORRIDORKEY_CK2_RUNTIME
		add_model_capabilities(capabilities);
#endif
		auto *properties = create_filter_properties(static_cast<Filter *>(data), capabilities);
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
		FilterInput::add_properties(properties);
#endif
		return properties;
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] could not create properties");
		return nullptr;
	}
}
void render_filter(void *data, gs_effect_t *)
{
	auto *filter = static_cast<Filter *>(data);
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
	if (filter->input.render())
		return;
#endif
	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(filter->source, obs_get_base_effect(OBS_EFFECT_DEFAULT), 0, 0);
}
gs_color_space filter_color_space(void *data, size_t count, const gs_color_space *preferred)
{
	auto *target = obs_filter_get_target(static_cast<Filter *>(data)->source);
	return target ? obs_source_get_color_space(target, count, preferred) : GS_CS_SRGB;
}
void tick_filter(void *data, float)
{
	static_cast<Filter *>(data)->sync->tick();
#if defined(CORRIDORKEY_ENABLE_FAKE_FILTER) || defined(CORRIDORKEY_CK2_RUNTIME)
	static_cast<Filter *>(data)->input.tick();
#else
	(void)data;
#endif
}
} // namespace
void register_filter()
{
	obs_source_info info{};
	info.id = "corridorkey";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = filter_name;
	info.create = create_filter;
	info.destroy = destroy_filter;
	info.update = update_filter;
	info.get_defaults = filter_defaults;
	info.get_properties = filter_properties;
	info.video_render = render_filter;
	info.video_tick = tick_filter;
	info.video_get_color_space = filter_color_space;
	obs_register_source(&info);
}
} // namespace corridorkey
