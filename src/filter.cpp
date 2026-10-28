// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>

#include "filter.hpp"

#include <obs-module.h>

namespace corridorkey {
namespace {
struct Filter {
	obs_source_t *source; // Borrowed from OBS; valid until destroy.
};

const char *filter_name(void *)
{
	return obs_module_text("Filter.Name");
}

void *create_filter(obs_data_t *, obs_source_t *source)
{
	return new Filter{source};
}

void destroy_filter(void *data)
{
	delete static_cast<Filter *>(data);
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "preset", "balanced");
	obs_data_set_default_string(settings, "model", "corridorkey-2");
	obs_data_set_default_bool(settings, "edge_refiner", true);
}

bool manage_models(obs_properties_t *, obs_property_t *, void *)
{
	// UI extension point: replace this disabled button with the Qt model manager.
	return false;
}

obs_properties_t *filter_properties(void *)
{
	auto *properties = obs_properties_create();
	auto *preset = obs_properties_add_list(properties, "preset", obs_module_text("Preset.Label"),
					       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset, obs_module_text("Preset.Facecam"), "facecam-60");
	obs_property_list_add_string(preset, obs_module_text("Preset.Balanced"), "balanced");
	obs_property_list_add_string(preset, obs_module_text("Preset.Quality"), "quality");
	obs_property_set_long_description(preset, obs_module_text("Preset.Help"));

	auto *model = obs_properties_add_list(properties, "model", obs_module_text("Model.Label"), OBS_COMBO_TYPE_LIST,
					      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(model, obs_module_text("Model.CorridorKey2"), "corridorkey-2");
	obs_property_list_add_string(model, obs_module_text("Model.CorridorKey1"), "corridorkey-1");
	obs_property_set_long_description(model, obs_module_text("Model.Help"));

	auto *refiner = obs_properties_add_bool(properties, "edge_refiner", obs_module_text("Refiner.Label"));
	obs_property_set_long_description(refiner, obs_module_text("Refiner.Help"));
	auto *manager =
		obs_properties_add_button(properties, "manage_models", obs_module_text("Models.Manage"), manage_models);
	obs_property_set_enabled(manager, false);
	obs_property_set_long_description(manager, obs_module_text("Models.Help"));

	obs_properties_add_text(properties, "status", obs_module_text("Status.Unavailable"), OBS_TEXT_INFO);
	return properties;
}

void render_filter(void *data, gs_effect_t *)
{
	const auto *filter = static_cast<Filter *>(data);
	// Render extension point: retain OBS's filter lifecycle when adding the GPU pipeline.
	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	obs_source_process_filter_end(filter->source, obs_get_base_effect(OBS_EFFECT_DEFAULT), 0, 0);
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
	info.get_defaults = filter_defaults;
	info.get_properties = filter_properties;
	info.video_render = render_filter;
	obs_register_source(&info);
}
} // namespace corridorkey
