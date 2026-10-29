// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "cache.hpp"
#include <obs-module.h>
#include <util/bmem.h>
namespace corridorkey::models {
fs::path obs_model_cache_path()
{
	char *raw = obs_module_config_path("models");
	require(raw != nullptr, "OBS configuration path unavailable", ErrorCode::io_failure);
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	return fs::path(reinterpret_cast<const char8_t *>(raw));
}
} // namespace corridorkey::models
