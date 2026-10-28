// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "properties.hpp"
#include <obs-module.h>

namespace corridorkey {
bool manage_models(obs_properties_t *, obs_property_t *, void *)
{
	// UI extension point: replace this disabled button with the Qt model manager.
	return false;
}

obs_properties_t *create_filter_properties(FilterControl *)
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
	auto *manager = obs_properties_add_button2(properties, "manage_models", obs_module_text("Models.Manage"),
						   manage_models, nullptr);
	obs_property_set_enabled(manager, false);
	obs_property_set_long_description(manager, obs_module_text("Models.Help"));

	obs_properties_add_text(properties, "status", obs_module_text("Status.Unavailable"), OBS_TEXT_INFO);
	return properties;
}

} // namespace corridorkey
