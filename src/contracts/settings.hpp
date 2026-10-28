// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "types.hpp"
#include <array>
#include <memory>
#include <string>

namespace corridorkey {
enum class Profile { facecam, balanced, quality, custom };
enum class ModelFamily { ck2, ck1 };
enum class GuideMode { refiner, rgb_guided, bilinear, chroma_legacy };
enum class UnmixMode { delta, compositing_equation };
enum class ScreenColor { green, blue };
enum class HintSource { chroma, modnet, mediapipe, ck2_stage1, external_mask, distilled };
enum class HintMode { off, previous, chroma, blend, external };
enum class OutputView { key, alpha, hint, foreground, crop, input, tiles, roi };
enum class InspectionView { transparent, checker, black, alpha, side_by_side };

struct QualitySettings {
	Profile profile = Profile::balanced;
	ModelFamily model = ModelFamily::ck2;
	std::string model_revision;
	Extent grid{512, 512};        // Width, height (unlike the design tables).
	std::uint32_t target_fps = 0; // Zero follows OBS.
	bool roi_enabled = true;
	std::uint32_t stage2_interval = 0;
	bool refiner_enabled = true;
	std::string refiner_model = "v12";
	std::uint32_t refiner_max_tiles = 512;
	GuideMode guide_mode = GuideMode::refiner;
	float guided_scale = 1;
	std::uint32_t guided_radius = 2;
	float guided_epsilon = 0.0005f;
	bool aspect_fix = false;
	std::uint32_t moge_interval = 3;
	float moge_motion_threshold = 0.05f;
	float roi_padding = 0.20f, roi_smoothing = 0.80f;
	bool use_rvm_temporal = true;
	float alpha_contrast = 1.05f, alpha_offset = 0;
	float clip_black = 0, clip_white = 1;
	float refiner_band_lo = 0.004f, refiner_band_gain = 10;
	UnmixMode unmix_mode = UnmixMode::delta;
	float unmix_inner = 1, despill_strength = 0.65f;
	ScreenColor screen_color = ScreenColor::green;
};
struct HintSettings {
	HintSource source = HintSource::chroma;
	float similarity = 0.40f, smoothness = 0.10f;
	std::uint32_t refresh_interval = 1;
	std::string mask_source_id;
	HintMode ck2_mode = HintMode::off;
	float temporal_quality = 0, chroma_quality = 0.75f, external_quality = 0.50f;
	std::uint32_t external_blur = 0;
};
struct BackendSettings {
	Backend backend = Backend::automatic;
	std::string device_id; // Empty selects the OBS adapter.
	Precision precision = Precision::automatic;
	TransportKind transport = TransportKind::automatic;
};
struct ContextSettings {
	Crop crop;
	OutputView output = OutputView::key;
	InspectionView inspection = InspectionView::transparent;
	std::array<float, 3> preview_color{0, 0, 0};
	bool bypass = false;
	bool auto_sync = false;
	std::string audio_source_id;
	std::int32_t sync_trim_ms = 0;
	std::uint32_t sync_limit_ms = 250, stale_timeout_ms = 250;
};
struct ExperimentalSettings {
	bool stagger_stages = false, head_pass = false;
	std::uint32_t head_size = 512, head_max_tiles = 256;
	float head_min_gain = 1.3f;
};

// Publish a fresh const snapshot; never mutate one already attached to a frame.
// UI expansion and temporary crop preview belong to W02, outside this snapshot.
struct Settings {
	std::uint32_t schema_version = 1;
	std::uint64_t revision = 1;
	QualitySettings quality;
	HintSettings hint;
	BackendSettings backend;
	ContextSettings context;
	ExperimentalSettings experimental;
};
using SettingsSnapshot = std::shared_ptr<const Settings>;
} // namespace corridorkey
