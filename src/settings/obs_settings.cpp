// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "obs_settings.hpp"
#include <string_view>

namespace corridorkey {
void set_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "preset", "balanced");
	obs_data_set_default_string(settings, "model", "corridorkey-2");
	obs_data_set_default_bool(settings, "edge_refiner", true);
}
SettingsSnapshot read_settings(obs_data_t *settings)
{
	auto snapshot = std::make_shared<Settings>();
	const std::string_view preset = obs_data_get_string(settings, "preset");
	if (preset == "facecam-60") {
		snapshot->quality.profile = Profile::facecam;
		snapshot->quality.grid = {512, 256};
		snapshot->quality.roi_enabled = false;
		snapshot->quality.target_fps = 60;
		snapshot->quality.refiner_max_tiles = 384;
	} else if (preset == "quality") {
		snapshot->quality.profile = Profile::quality;
		snapshot->quality.stage2_interval = 1;
		snapshot->quality.refiner_max_tiles = 768;
	}
	if (std::string_view(obs_data_get_string(settings, "model")) == "corridorkey-1")
		snapshot->quality.model = ModelFamily::ck1;
	snapshot->quality.refiner_enabled = obs_data_get_bool(settings, "edge_refiner");
	return snapshot;
}
} // namespace corridorkey
