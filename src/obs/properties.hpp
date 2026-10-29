// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <obs.h>
#include <map>
#include <string>
#include <vector>

namespace corridorkey {
class FilterControl;
// W04/W06 supply only verified, installed variants and successfully probed choices.
// Missing entries expose saved choices as unavailable, with an explanation.
struct PropertyCapabilities {
	std::map<std::string, std::vector<std::string>, std::less<>> choices;
	bool ck1_refiner = false, head_pass = false, stagger = false;
	bool external_mask = false, guided_scale = false, audio_sync = false;
};
obs_properties_t *create_filter_properties(FilterControl *control, PropertyCapabilities capabilities = {});
} // namespace corridorkey
