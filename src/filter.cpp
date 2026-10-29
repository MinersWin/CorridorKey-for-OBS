// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "filter.hpp"
#include "obs/filter_control.hpp"
#include "obs/properties.hpp"
#include "settings/obs_settings.hpp"
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
#include "obs/filter_input.hpp"
#endif
#include <obs-module.h>
#include <mutex>

namespace corridorkey {
namespace {
struct Filter final : FilterControl {
	explicit Filter(obs_source_t *owner)
		: source(owner)
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
		  ,
		  input(owner)
#endif
	{
	}
	obs_source_t *source;
	mutable std::mutex control;
	SettingsSnapshot settings;
	SettingsSnapshot settings_snapshot() const override
	{
		std::lock_guard lock(control);
		return settings;
	}
	StatusSnapshot status_snapshot() const override
	{
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
		return input.status();
#else
		auto status = std::make_shared<Status>();
		status->lifecycle = Lifecycle::bypassed;
		status->failure = error(ErrorCode::model_missing);
		return status;
#endif
	}
	void request_reset() noexcept override
	{
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
		input.reset();
#endif
	}
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
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
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
		filter->input.update(settings, snapshot);
#endif
		std::lock_guard lock(filter->control);
		filter->settings = std::move(snapshot);
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] settings update failed; retaining previous settings");
	}
}
void enable_filter(void *data, calldata_t *)
{
	static_cast<Filter *>(data)->request_reset();
}
void *create_filter(obs_data_t *settings, obs_source_t *source)
{
	try {
		auto filter = std::make_unique<Filter>(source);
		update_filter(filter.get(), settings);
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
		auto *properties = create_filter_properties(static_cast<Filter *>(data));
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
		FilterInput::add_properties(properties);
		if (data) {
			const auto status = static_cast<Filter *>(data)->status_snapshot();
			obs_property_set_description(obs_properties_get(properties, "status"),
						     status->diagnostic_detail.c_str());
		}
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
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
	if (filter->input.render())
		return;
#endif
	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(filter->source, obs_get_base_effect(OBS_EFFECT_DEFAULT), 0, 0);
}
void tick_filter(void *data, float)
{
#ifdef CORRIDORKEY_ENABLE_FAKE_FILTER
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
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = filter_name;
	info.create = create_filter;
	info.destroy = destroy_filter;
	info.update = update_filter;
	info.get_defaults = filter_defaults;
	info.get_properties = filter_properties;
	info.video_render = render_filter;
	info.video_tick = tick_filter;
	obs_register_source(&info);
}
} // namespace corridorkey
