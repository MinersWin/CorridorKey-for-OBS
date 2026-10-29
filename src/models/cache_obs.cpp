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
#ifdef _WIN32
	// OBS builds without char8_t on MSVC; u8path is the UTF-8 constructor there.
	return fs::u8path(raw);
#else
	// POSIX paths are byte strings, and OBS hands out UTF-8.
	return fs::path(raw);
#endif
}
} // namespace corridorkey::models
