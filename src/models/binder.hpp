// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "cache.hpp"

namespace corridorkey::models {
using BindingProgress = std::function<void(Progress)>;
struct BindingResult {
	std::uint64_t bytes = 0;
	std::string sha256;
};
// Blocking worker API. Source IDs resolve to local files, never downloaded here.
// Verifies every source pin before parsing. Callbacks run on the calling thread.
BindingResult bind_weights(std::string_view export_json, const std::map<std::string, fs::path> &sources,
			   const fs::path &output, std::string_view expected_sha256 = {}, std::stop_token = {},
			   BindingProgress = {});
// Caller holds W04's revision lock and has prepared the staging directory.
void bind_package(const Manifest &, Cache &, std::stop_token = {}, BindingProgress = {});
} // namespace corridorkey::models
