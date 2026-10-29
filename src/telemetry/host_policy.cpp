// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_policy.hpp"
#include <cstdint>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace corridorkey::diagnostics {
double frame_rate(std::uint32_t numerator, std::uint32_t denominator, std::optional<double> active)
{
	return active && std::isfinite(*active) && *active > 0 ? *active
	       : denominator                                   ? static_cast<double>(numerator) / denominator
							       : 0;
}
HostPolicy assess_host(std::uint32_t version, std::string_view, const SymbolLookup &lookup)
{
	HostPolicy out;
	for (const char *required : {"obs_get_version", "obs_register_source_s"}) {
		if (!lookup(required)) {
			out.load = false;
			out.message = std::string("Incompatible required libobs ABI: missing ") + required +
				      ". Install a compatible OBS 30.2+ build and the plugin for that host.";
			return out;
		}
	}
	const auto major = version >> 24, minor = (version >> 16) & 0xff;
	out.unreviewed = major < 30 || (major == 30 && minor < 2) || major > 33;
	out.active_fps = lookup("obs_get_active_fps") != nullptr;
	if (out.unreviewed)
		out.message =
			"Untested OBS version; continuing with capability checks. Report problems through the issue chooser.";
	return out;
}
void *obs_symbol(const char *name)
{
#ifdef _WIN32
	const auto module = GetModuleHandleW(L"obs.dll");
	return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
#else
	return dlsym(RTLD_DEFAULT, name);
#endif
}
} // namespace corridorkey::diagnostics
