// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>

#include <obs-module.h>
#include <string>

#include "filter.hpp"
#include "telemetry/host_policy.hpp"
#ifdef CORRIDORKEY_MODEL_MANAGER
#include "ui/model_manager.hpp"
#include "packaging/runtime_dialog.hpp"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("corridorkey", "en-US")
OBS_MODULE_AUTHOR("Moritz Mantel")

MODULE_EXPORT const char *obs_module_description(void)
{
	static const auto description =
		std::string("CorridorKey ") + CORRIDORKEY_VERSION + ". " + obs_module_text("Module.Description");
	return description.c_str();
}

bool obs_module_load(void)
{
	const auto host_version = obs_get_version();
	const auto policy = corridorkey::diagnostics::assess_host(host_version, obs_get_version_string(),
								  corridorkey::diagnostics::obs_symbol);
	if (!policy.load) {
		blog(LOG_ERROR, "[corridorkey] %s", policy.message.c_str());
		return false;
	}
	blog(LOG_INFO, "[corridorkey] OBS host %s; compiled against API %u.%u", obs_get_version_string(),
	     static_cast<unsigned>(LIBOBS_API_MAJOR_VER), static_cast<unsigned>(LIBOBS_API_MINOR_VER));
	if (policy.unreviewed) {
		blog(LOG_WARNING,
		     "[corridorkey] unreviewed OBS version; continuing with capability checks. "
		     "Report compatibility issues at https://github.com/MinersWin/CorridorKey-for-OBS/issues");
	}
	// New optional APIs must be resolved at runtime before use. Keep pass-through
	// available if inference, a provider, or a future UI capability is unavailable.
	corridorkey::register_filter();
#ifdef CORRIDORKEY_MODEL_MANAGER
	corridorkey::ui::register_model_manager();
	corridorkey::packaging::register_runtime_manager();
#endif
	blog(LOG_INFO, "[corridorkey] loaded (version %s)", CORRIDORKEY_VERSION);
	return true;
}

void obs_module_unload(void)
{
#ifdef CORRIDORKEY_MODEL_MANAGER
	corridorkey::ui::shutdown_model_manager();
	corridorkey::packaging::shutdown_runtime_manager();
#endif
	blog(LOG_INFO, "[corridorkey] unloaded");
}
