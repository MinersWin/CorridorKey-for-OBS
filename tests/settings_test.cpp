// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "settings/schema.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <limits>
using namespace corridorkey;
using namespace corridorkey::settings;

TEST_CASE("settings shipped defaults follow the reference profiles")
{
	auto b = defaults();
	auto f = defaults(Profile::facecam);
	auto q = defaults(Profile::quality);
	CHECK(b.quality.grid == Extent{512, 512});
	CHECK(b.quality.roi_enabled);
	CHECK(b.quality.refiner_max_tiles == 512);
	CHECK(b.quality.stage2_interval == 0);
	CHECK(f.quality.grid == Extent{512, 256});
	CHECK_FALSE(f.quality.roi_enabled);
	CHECK(f.quality.target_fps == 60);
	CHECK(f.quality.refiner_max_tiles == 384);
	CHECK(q.quality.refiner_max_tiles == 768);
	CHECK(q.quality.stage2_interval == 1);
	for (const auto &s : {b, f, q}) {
		CHECK(s.quality.moge_motion_threshold == doctest::Approx(.05));
		CHECK(s.quality.guided_epsilon == doctest::Approx(.0005));
		CHECK(s.quality.guided_radius == 2);
		CHECK(s.quality.despill_strength == doctest::Approx(.65));
		CHECK(s.quality.alpha_contrast == doctest::Approx(1.05));
		CHECK(s.hint.ck2_mode == HintMode::off);
		CHECK(s.hint.temporal_quality == 0);
		CHECK(s.context.output == OutputView::key);
		CHECK_FALSE(s.context.auto_sync);
	}
	auto ck1 = defaults(Profile::balanced, ModelFamily::ck1);
	CHECK(ck1.quality.profile == Profile::custom);
	CHECK(ck1.quality.grid == Extent{384, 384});
	CHECK_FALSE(ck1.quality.refiner_enabled);
	CHECK(ck1.quality.guide_mode == GuideMode::rgb_guided);
	CHECK(ck1.hint.source == HintSource::chroma);
}
TEST_CASE("quality edits select Custom while context survives explicit presets")
{
	auto s = defaults(Profile::facecam);
	edit(s, "cut_right", 12.5);
	edit(s, "sync_trim_ms", std::int64_t{27});
	edit(s, "audio_source_id", std::string("microphone-uuid"));
	edit(s, "obs_view", std::string("alpha"));
	CHECK(s.quality.profile == Profile::facecam);
	edit(s, "alpha_contrast", 1.2);
	CHECK(s.quality.profile == Profile::custom);
	edit(s, "backend", std::string("cuda"));
	const auto crop = s.context.crop;
	apply_preset(s, Profile::quality);
	CHECK(s.context.crop == crop);
	CHECK(s.context.sync_trim_ms == 27);
	CHECK(s.context.audio_source_id == "microphone-uuid");
	CHECK(s.context.output == OutputView::alpha);
	CHECK(s.quality.alpha_contrast == doctest::Approx(1.05));
	CHECK(s.quality.refiner_max_tiles == 768);
	CHECK(s.backend.backend == Backend::automatic);
}
TEST_CASE("pipeline banks retain independent edits through repeated switches")
{
	Editor e{defaults(Profile::facecam), {}};
	edit(e.current, "backend", std::string("cuda"));
	edit(e.current, "alpha_contrast", 1.22);
	edit(e.current, "cut_left", 14.0);
	e.select_model(ModelFamily::ck1);
	CHECK(e.current.quality.grid == Extent{384, 384});
	CHECK(e.current.backend.backend == Backend::automatic);
	edit(e.current, "backend", std::string("directml"));
	edit(e.current, "hint_source", std::string("modnet"));
	edit(e.current, "despill_strength", .32);
	e.select_model(ModelFamily::ck2);
	CHECK(e.current.quality.grid == Extent{512, 256});
	CHECK(e.current.backend.backend == Backend::cuda);
	CHECK(e.current.quality.alpha_contrast == doctest::Approx(1.22));
	e.select_model(ModelFamily::ck1);
	CHECK(e.current.hint.source == HintSource::modnet);
	CHECK(e.current.backend.backend == Backend::directml);
	CHECK(e.current.quality.despill_strength == doctest::Approx(.32));
	CHECK(e.current.context.crop.left == doctest::Approx(.14));
	e.select_preset(Profile::quality);
	CHECK(e.current.quality.model == ModelFamily::ck2);
	CHECK(e.current.context.crop.left == doctest::Approx(.14));
}
TEST_CASE("Lua absolute crop migration is idempotent and respects percentage cuts")
{
	Record r{{"crop_left", .1}, {"crop_top", .2}, {"crop_right", .8}, {"crop_bottom", .7}};
	auto s = decode(r);
	CHECK(s.context.crop == Crop{.1f, .2f, .8f, .7f});
	r["cut_right"] = 5.0;
	s = decode(r);
	CHECK(s.context.crop.right == doctest::Approx(.95));
	const auto again = decode(encode(s));
	CHECK(again.context.crop == s.context.crop);
	r = {{"crop_left", -100.0}, {"crop_right", -20.0}, {"crop_top", 99.0}};
	s = decode(r);
	CHECK(s.context.crop.left == 0);
	CHECK(s.context.crop.right == doctest::Approx(.55));
	CHECK(s.context.crop.top == doctest::Approx(.45));
	CHECK(s.context.crop.bottom == 1);
}
TEST_CASE("legacy aliases and grid names migrate without losing saved values")
{
	Record r{{"preset", std::string("fast_4k_512x256_s1s2")},
		 {"profile", std::string("facecam60")},
		 {"guided_log_eps", -4.0},
		 {"moge_motion_threshold", .035},
		 {"stage2_interval", std::int64_t{0}},
		 {"refiner_model", std::string("refiner_v12_w24.pt")},
		 {"hint_mode", std::string("carry")},
		 {"obs_view", std::string("composite")},
		 {"view_mode", std::string("checker")},
		 {"use_cradio", true}};
	std::vector<std::string> notices;
	auto s = decode(r, &notices);
	CHECK(s.quality.grid == Extent{512, 256});
	CHECK(s.quality.profile == Profile::facecam);
	CHECK(s.quality.moge_motion_threshold == doctest::Approx(.035));
	CHECK(s.quality.guided_epsilon == doctest::Approx(.0001));
	CHECK(s.quality.refiner_model == "v12");
	CHECK(s.quality.stage2_interval == 0);
	CHECK(s.context.inspection == InspectionView::transparent);
	CHECK_FALSE(notices.empty());
	for (const auto &[key, grid] : std::map<std::string, Extent>{{"roi_ultra_4k_512x512_s1s2", {512, 512}},
								     {"high_aux_4k_1024x512_s1", {1024, 512}},
								     {"quality_4k_768x512_s1s2", {768, 512}},
								     {"extreme_async_4k_1024x512", {1024, 512}}})
		CHECK(decode({{"preset", key}}).quality.grid == grid);
	CHECK(decode({{"model", std::string("corridorkey-1")}, {"edge_refiner", false}}).quality.model ==
	      ModelFamily::ck1);
}
TEST_CASE("invalid numbers and types cannot escape ranges")
{
	Record r{{"alpha_contrast", 100.0},
		 {"alpha_offset", -5.0},
		 {"guided_radius", std::int64_t{-100}},
		 {"guided_eps", std::numeric_limits<double>::quiet_NaN()},
		 {"moge_interval", 1e100},
		 {"clip_black", .5},
		 {"clip_white", .5},
		 {"refiner_max_tiles", std::int64_t{0}},
		 {"cut_top", std::numeric_limits<double>::infinity()},
		 {"target_fps", std::string("999")}};
	auto s = decode(r);
	CHECK(s.quality.alpha_contrast == doctest::Approx(1.5));
	CHECK(s.quality.alpha_offset == doctest::Approx(-.2));
	CHECK(s.quality.guided_radius == 1);
	CHECK(s.quality.guided_epsilon == doctest::Approx(.0005));
	CHECK(s.quality.moge_interval == 8);
	CHECK(s.quality.clip_white >= s.quality.clip_black + .0099f);
	CHECK(s.quality.refiner_max_tiles == 64);
	CHECK(s.context.crop.top == 0);
	CHECK(s.quality.target_fps == 0);
}
TEST_CASE("complete settings record round trips all fields and model revisions")
{
	auto s = defaults(Profile::quality);
	s.quality.model_revision = "verified-revision";
	s.context.preview_color = {1, .4f, .2f};
	edit(s, "cut_bottom", 22.5);
	edit(s, "hint_mode", std::string("external"));
	edit(s, "hint_mask", std::string("mask-uuid"));
	edit(s, "external_hint_quality", .7);
	const auto once = encode(s);
	const auto twice = encode(decode(once));
	REQUIRE(once.size() == twice.size());
	for (const auto &[key, value] : once) {
		REQUIRE(twice.contains(key));
		if (auto *p = std::get_if<double>(&value))
			CHECK(std::get<double>(twice.at(key)) == doctest::Approx(*p));
		else
			CHECK(value == twice.at(key));
	}
	CHECK(std::get<std::int64_t>(twice.at("settings_version")) == schema_version);
	CHECK(std::get<std::string>(twice.at("model_revision")) == "verified-revision");
}

TEST_CASE("corrupt legacy numeric list values fall back without narrowing overflow")
{
	auto s = decode({{"target_fps", 1e100},
			 {"stage2_interval", -1e100},
			 {"guided_scale", std::numeric_limits<double>::infinity()}});
	CHECK(s.quality.target_fps == 0);
	CHECK(s.quality.stage2_interval == 0);
	CHECK(s.quality.guided_scale == 1);
}
