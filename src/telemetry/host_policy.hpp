// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace corridorkey::diagnostics {
using SymbolLookup = std::function<void *(const char *)>;
struct HostPolicy {
	bool load = true, unreviewed = false, active_fps = false;
	std::string message;
};
HostPolicy assess_host(std::uint32_t version, std::string_view version_text, const SymbolLookup &);
void *obs_symbol(const char *);
double frame_rate(std::uint32_t numerator, std::uint32_t denominator, std::optional<double> active);
} // namespace corridorkey::diagnostics
