// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>

#include <obs-module.h>

#include "filter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("corridorkey", "en-US")
OBS_MODULE_AUTHOR("Moritz Mantel")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Module.Description");
}

bool obs_module_load(void)
{
	corridorkey::register_filter();
	blog(LOG_INFO, "[corridorkey] loaded (version %s; pass-through foundation)", CORRIDORKEY_VERSION);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[corridorkey] unloaded");
}
