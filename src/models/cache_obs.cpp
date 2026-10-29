// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "cache.hpp"
#include "util/utf8_path.hpp"
#include <obs-module.h>
#include <util/bmem.h>
namespace corridorkey::models {
fs::path obs_model_cache_path()
{
	char *raw = obs_module_config_path("models");
	require(raw != nullptr, "OBS configuration path unavailable", ErrorCode::io_failure);
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	return utf8_path(raw);
}
} // namespace corridorkey::models
