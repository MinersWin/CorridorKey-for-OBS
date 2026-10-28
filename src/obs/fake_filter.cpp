// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "fake_filter.hpp"
#include <obs-module.h>
#include <algorithm>

namespace corridorkey {
FakeFilterPreview::FakeFilterPreview() : input_(synthetic_rgb({64, 64}))
{
	obs_enter_graphics();
	texture_ = gs_texture_create(64, 64, GS_RGBA, 1, nullptr, GS_DYNAMIC);
	obs_leave_graphics();
}
FakeFilterPreview::~FakeFilterPreview()
{
	pipeline_.shutdown();
	obs_enter_graphics();
	gs_texture_destroy(texture_);
	obs_leave_graphics();
}
void FakeFilterPreview::update(obs_data_t *settings, SettingsSnapshot snapshot)
{
	std::lock_guard lock(control_);
	enabled_ = obs_data_get_bool(settings, "w01_fake_enabled");
	generation_ = pool_.advance_generation();
	settings_ = std::move(snapshot);
	FakeOptions options;
	options.delay =
		std::chrono::milliseconds(std::clamp<long long>(obs_data_get_int(settings, "w01_fake_delay"), 0, 2000));
	options.reorder_delay = obs_data_get_bool(settings, "w01_fake_reorder") ? std::chrono::milliseconds(100)
										: std::chrono::milliseconds(0);
	const auto fault = obs_data_get_int(settings, "w01_fake_fault");
	if (fault >= 0 && fault <= 3)
		options.fault = static_cast<FakeFault>(fault);
	pipeline_.configure(options);
	PrepareRequest request;
	request.instance = 1; // Each preview owns a separate pipeline and pool.
	request.generation = generation_;
	request.settings = settings_;
	if (!enabled_ || pipeline_.prepare(request)) {
		enabled_ = false;
		pipeline_.cancel();
	}
}
void FakeFilterPreview::cancel() noexcept
{
	pipeline_.cancel();
}
bool FakeFilterPreview::render(std::uint32_t width, std::uint32_t height) noexcept
{
	SettingsSnapshot settings;
	Generation generation;
	{
		std::unique_lock lock(control_, std::try_to_lock);
		if (!lock.owns_lock())
			return false;
		if (!enabled_ || !texture_ || !width || !height) {
			presented_.reset();
			return false;
		}
		settings = settings_;
		generation = generation_;
	}
	const auto now = Clock::now();
	if (display_generation_ != generation) {
		presented_.reset();
		display_generation_ = generation;
		last_presented_ = 0;
	}
	const auto tick = obs_get_video_frame_time();
	if (tick != last_tick_) {
		last_tick_ = tick;
		if (auto lease = pool_.try_acquire()) {
			InputFrame frame;
			frame.lease = std::move(*lease);
			frame.lease.transition(FrameState::ready);
			frame.identity = {1, generation, ++sequence_, tick, now};
			frame.transform.source_size = input_.size;
			frame.original = frame.analysis = input_;
			frame.settings = std::move(settings);
			pipeline_.try_submit(frame, now);
		}
	}
	// At most three polls; no drain loop can grow with producer activity.
	for (std::size_t i = 0; i < FramePool::capacity; ++i) {
		auto result = pipeline_.poll_result();
		if (!result)
			break;
		if (result->identity.generation != generation || result->identity.sequence < last_presented_)
			continue;
		if (result->failure) {
			presented_.reset();
			continue;
		}
		if (!presentable(*result, 1, generation, last_presented_, now, std::chrono::milliseconds(250)))
			continue;
		result->input.lease.transition(FrameState::presenting);
		presented_ = std::move(result);
		last_presented_ = presented_->identity.sequence;
		const auto rgb = presented_->foreground.storage->host_floats();
		const auto alpha = presented_->alpha.storage->host_floats();
		for (std::size_t pixel = 0; pixel < alpha.size(); ++pixel) {
			for (std::size_t channel = 0; channel < 3; ++channel)
				pixels_[pixel * 4 + channel] =
					static_cast<std::uint8_t>(255 * rgb[pixel * 3 + channel] * alpha[pixel]);
			pixels_[pixel * 4 + 3] = static_cast<std::uint8_t>(255 * alpha[pixel]);
		}
		gs_texture_set_image(texture_, pixels_.data(), 64 * 4, false);
	}
	if (!presented_ || !presentable(*presented_, 1, generation, presented_->identity.sequence, now,
					std::chrono::milliseconds(250))) {
		presented_.reset();
		return false;
	}
	auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture_);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(texture_, 0, width, height);
	gs_blend_state_pop();
	return true;
}
void FakeFilterPreview::add_properties(obs_properties_t *properties)
{
	auto *group = obs_properties_create();
	obs_properties_add_bool(group, "w01_fake_enabled", "Show synthetic test image");
	obs_properties_add_int(group, "w01_fake_delay", "Fake delay (ms)", 0, 2000, 1);
	obs_properties_add_bool(group, "w01_fake_reorder", "Reorder completions");
	auto *fault = obs_properties_add_list(group, "w01_fake_fault", "Injected failure", OBS_COMBO_TYPE_LIST,
					      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fault, "None", 0);
	obs_property_list_add_int(fault, "Out of memory", 1);
	obs_property_list_add_int(fault, "Backend unavailable", 2);
	obs_property_list_add_int(fault, "Invalid output", 3);
	obs_properties_add_group(properties, "w01_fake", "Synthetic diagnostics (replaces video)", OBS_GROUP_NORMAL,
				 group);
}
} // namespace corridorkey
