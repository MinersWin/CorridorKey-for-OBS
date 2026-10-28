// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "engine/fake_pipeline.hpp"
#include <obs.h>

namespace corridorkey {
// Diagnostic builds only. Displays a synthetic image, never a key of the camera.
class FakeFilterPreview {
public:
	FakeFilterPreview();
	~FakeFilterPreview();
	void update(obs_data_t *settings, SettingsSnapshot snapshot);
	bool render(std::uint32_t width, std::uint32_t height) noexcept;
	void cancel() noexcept;
	static void add_properties(obs_properties_t *properties);

private:
	FramePool pool_;
	FakePipeline pipeline_;
	Image input_;
	SettingsSnapshot settings_;
	std::mutex control_;
	std::optional<FrameResult> presented_;
	gs_texture_t *texture_ = nullptr;
	std::array<std::uint8_t, 64 * 64 * 4> pixels_{};
	bool enabled_ = false;
	Generation generation_ = 1;
	Sequence sequence_ = 0;
	Sequence last_presented_ = 0;
	Generation display_generation_ = 0;
	std::uint64_t last_tick_ = 0;
};
} // namespace corridorkey
