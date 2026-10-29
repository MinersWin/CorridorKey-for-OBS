// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/manager_model.hpp"
#include "engine/providers/runtime.hpp"
#include <functional>

namespace corridorkey::diagnostics {
struct Instance {
	SettingsSnapshot settings;
	StatusSnapshot status;
	Extent source;
};
struct Provider {
	Backend backend = Backend::automatic;
	std::optional<ort::Bundle> bundle;
	std::optional<ProbeResult> probe;
};
struct Environment {
	std::string plugin, commit, obs, renderer, os, kernel, architecture, qt;
	std::vector<ort::Device> devices;
	std::map<std::string, std::uint64_t> vram_bytes;
	std::vector<Provider> providers;
	std::vector<Instance> instances;
	Extent canvas;
	double fps = 0;
	bool streaming = false, recording = false, active_fps_api = false;
};
// Control-side callbacks are removed before the owning filter starts teardown.
void register_instance(const void *, std::function<Instance()>);
void unregister_instance(const void *);
std::vector<Instance> instances();
void record_error(ErrorCode) noexcept;
void record_probe(const ProbeResult &);
std::optional<ProbeResult> latest_probe(Backend);
std::string redact(std::string_view);
std::string report(const ui::ManagerSnapshot &, const Environment &);
} // namespace corridorkey::diagnostics
