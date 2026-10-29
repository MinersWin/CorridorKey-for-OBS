// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "schema.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace corridorkey::settings {
namespace {
std::string number_string(double n)
{
	if (!std::isfinite(n) || n < 0 || n > 10000)
		return {};
	return n == 0.5 ? "0.5" : std::to_string(static_cast<int>(n));
}
std::string grid_string(Extent e)
{
	return std::to_string(e.height) + "x" + std::to_string(e.width);
}
Extent parse_grid(const std::string &v)
{
	const auto split = v.find('x');
	return {static_cast<std::uint32_t>(std::stoul(v.substr(split + 1))),
		static_cast<std::uint32_t>(std::stoul(v.substr(0, split)))};
}
std::int64_t pack_color(const std::array<float, 3> &c)
{
	auto byte = [](float v) {
		return static_cast<std::int64_t>(
			std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f) * 255));
	};
	return byte(c[0]) | (byte(c[1]) << 8) | (byte(c[2]) << 16) | 0xff000000LL;
}
std::array<float, 3> unpack_color(std::int64_t c)
{
	return {static_cast<float>(c & 255) / 255, static_cast<float>((c >> 8) & 255) / 255,
		static_cast<float>((c >> 16) & 255) / 255};
}
#include "fields.inc"
double numeric(const Value &v, double fallback)
{
	if (auto *p = std::get_if<double>(&v))
		return std::isfinite(*p) ? *p : fallback;
	if (auto *p = std::get_if<std::int64_t>(&v))
		return static_cast<double>(*p);
	return fallback;
}
std::string string(const Record &r, std::string_view k, std::string fallback = {})
{
	auto it = r.find(k);
	if (it != r.end())
		if (auto *p = std::get_if<std::string>(&it->second))
			return *p;
	return fallback;
}
void alias(Record &r, const char *old_key, const char *key)
{
	if (!r.contains(key) && r.contains(old_key))
		r[key] = r.at(old_key);
}
void assign(Settings &s, const Field &f, const Value &v)
{
	const auto fallback = f.get(s);
	switch (f.kind) {
	case Kind::boolean:
		if (std::holds_alternative<bool>(v))
			f.set(s, v);
		break;
	case Kind::integer:
		f.set(s, static_cast<std::int64_t>(std::clamp(numeric(v, numeric(fallback, 0)), f.minimum, f.maximum)));
		break;
	case Kind::number:
		f.set(s, std::clamp(numeric(v, numeric(fallback, 0)), f.minimum, f.maximum));
		break;
	case Kind::color:
		f.set(s, static_cast<std::int64_t>(std::clamp(numeric(v, 0), 0.0, 4294967295.0)));
		break;
	case Kind::choice:
		if (auto *p = std::get_if<std::string>(&v))
			if (std::ranges::any_of(f.choices, [&](auto c) { return *p == c.value; }))
				f.set(s, v);
		break;
	case Kind::text:
		if (auto *p = std::get_if<std::string>(&v); p && p->size() <= 1024)
			f.set(s, v);
		break;
	}
}
const LegacyField legacy[] = {
	{"input_crop/crop_left/crop_top/crop_right/crop_bottom/crop", Disposition::derived,
	 "cut_left/cut_top/cut_right/cut_bottom"},
	{"alpha_clip", Disposition::derived, "clip_black/clip_white"},
	{"guided_log_eps", Disposition::derived, "guided_eps (exact); log10 UI"},
	{"preset (five QualityPreset values)", Disposition::active, "grid/roi_enabled/stage2_interval"},
	{"profile/model/edge_refiner", Disposition::active, "profile/model_id/refiner_enabled"},
	{"refiner_path/refiner", Disposition::active, "refiner_model (verified package ID; no arbitrary paths)"},
	{"external_hint", Disposition::experimental, "hint_mask"},
	{"auto_audio_source", Disposition::active, "audio_source_id (resolve legacy name before use)"},
	{"output_width/output_height/engine_resolution", Disposition::derived, "parent source dimensions"},
	{"async_4k_post", Disposition::derived, "asynchronous worker"},
	{"refiner_tile/refiner_halo/refiner_band_hi", Disposition::derived,
	 "manifest constants: v12 center 32, halo 10, upper band .996"},
	{"refiner_max_tiles=0", Disposition::retired, "bounded live batches; retain notice, use minimum 64"},
	{"use_cradio", Disposition::retired, "off; migration notice"},
	{"post_v3/compile_post/triton_post/triton_dw/compile_refiner/compile_mode", Disposition::retired,
	 "verified native post path / backend-owned compilation"},
	{"engine_auto_start", Disposition::retired, "OBS enable/show lifecycle"},
	{"python_exe/project_root/package_root", Disposition::retired, "no external Python process"},
	{"btn_engine_start/btn_engine_stop/btn_engine_restart", Disposition::retired, "enable/bypass/retry backend"},
	{"btn_engine_log/telemetry_status", Disposition::derived, "status/diagnostics"},
	{"input_source/camera/video/dshow/spout-in/synthetic", Disposition::retired,
	 "OBS parent source; synthetic input is test-only"},
	{"spout/spout_name/vcam/no_preview/control_port", Disposition::retired,
	 "native OBS filter; no remote control socket"},
	{"fps/max_frames", Disposition::retired, "target_fps / test-only frame count"},
	{"command/reopen", Disposition::retired, "teardown/reset memory"},
	{"crop_preview", Disposition::derived, "temporary properties-only view; not persisted in Settings"},
	{"advanced/expert/show_details", Disposition::derived, "UI preferences outside processing snapshots"},
};
} // namespace
std::span<const Field> inventory()
{
	return fields;
}
std::span<const LegacyField> legacy_inventory()
{
	return legacy;
}
const Field *find(std::string_view key)
{
	for (const auto &f : fields)
		if (key == f.key)
			return &f;
	return nullptr;
}
Settings defaults(Profile profile, ModelFamily model)
{
	Settings s;
	s.schema_version = static_cast<std::uint32_t>(schema_version);
	s.quality.profile = profile;
	s.quality.model = model;
	if (model == ModelFamily::ck1) {
		s.quality.profile = Profile::custom;
		s.quality.grid = {384, 384};
		s.quality.roi_enabled = false;
		s.quality.refiner_enabled = false;
		s.quality.guide_mode = GuideMode::rgb_guided;
	} else if (profile == Profile::facecam) {
		s.quality.grid = {512, 256};
		s.quality.target_fps = 60;
		s.quality.roi_enabled = false;
		s.quality.refiner_max_tiles = 384;
	} else if (profile == Profile::quality) {
		s.quality.stage2_interval = 1;
		s.quality.refiner_max_tiles = 768;
	}
	return s;
}
void apply_preset(Settings &s, Profile p)
{
	if (p == Profile::custom) {
		s.quality.profile = p;
		return;
	}
	const auto context = s.context;
	const auto revision = s.revision;
	const auto model_revision = s.quality.model == ModelFamily::ck2 ? s.quality.model_revision : std::string{};
	s = defaults(p);
	s.context = context;
	s.quality.model_revision = model_revision;
	s.revision = revision + 1;
}
void clamp(Settings &s)
{
	const auto copy = s;
	s = defaults(copy.quality.profile, copy.quality.model);
	s.revision = copy.revision;
	for (const auto &f : fields)
		assign(s, f, f.get(copy));
	s.quality.clip_white = std::max(s.quality.clip_white, s.quality.clip_black + .01f);
	// Off is RGB guided; selecting a method updates the boolean in edit().
	if (!s.quality.refiner_enabled && s.quality.guide_mode == GuideMode::refiner)
		s.quality.guide_mode = GuideMode::rgb_guided;
	s.schema_version = static_cast<std::uint32_t>(schema_version);
}
Record encode(const Settings &s)
{
	Record r;
	for (const auto &f : fields)
		r[f.key] = f.get(s);
	r["settings_version"] = schema_version;
	return r;
}
Settings decode(Record r, std::vector<std::string> *notices)
{
	auto notice = [&](const char *n) {
		if (notices)
			notices->emplace_back(n);
	};
	const auto version = r.contains("settings_version") ? numeric(r.at("settings_version"), 0) : 0;
	const bool legacy_version = version < schema_version;
	if (version > schema_version)
		notice("Migration.Newer");
	if (legacy_version) {
		alias(r, "model", "model_id");
		alias(r, "edge_refiner", "refiner_enabled");
		auto model = string(r, "model_id");
		if (model == "corridorkey-1")
			r["model_id"] = std::string("ck1");
		if (model == "corridorkey-2")
			r["model_id"] = std::string("ck2");
		const auto old_preset = string(r, "preset");
		if (!r.contains("profile") &&
		    (old_preset == "balanced" || old_preset == "quality" || old_preset == "facecam-60"))
			r["profile"] = old_preset;
		if (string(r, "profile") == "facecam60")
			r["profile"] = std::string("facecam-60");
		if (!r.contains("grid") && old_preset.find("4k") != std::string::npos) {
			const bool fast = old_preset == "fast_4k_512x256_s1s2";
			const bool wide = old_preset == "high_aux_4k_1024x512_s1" ||
					  old_preset == "extreme_async_4k_1024x512";
			const bool quality = old_preset == "quality_4k_768x512_s1s2";
			r["grid"] = std::string(fast ? "256x512" : wide ? "512x1024" : quality ? "512x768" : "512x512");
			if (!r.contains("roi_enabled"))
				r["roi_enabled"] = !fast && !wide && !quality;
			if (!r.contains("profile"))
				r["profile"] = std::string("custom");
			if (!r.contains("stage2_interval"))
				r["stage2_interval"] = std::string(quality ? "1" : "0");
			notice("Migration.Grid");
		}
		// The Lua named profiles overrode these raw controls when submitting frames.
		if (old_preset.find("4k") != std::string::npos && string(r, "profile") != "custom") {
			const auto name = string(r, "profile");
			if (name == "balanced" || name == "facecam-60" || name == "quality") {
				const auto effective = defaults(name == "facecam-60" ? Profile::facecam
								: name == "quality"  ? Profile::quality
										     : Profile::balanced);
				const auto values = encode(effective);
				for (const auto *key :
				     {"grid", "roi_enabled", "stage2_interval", "guide_mode", "refiner_enabled",
				      "refiner_max_tiles", "stagger_s1_s2", "temporal_hint_quality"})
					r[key] = values.at(key);
			}
		}
		for (const auto *edge : {"left", "top", "right", "bottom"}) {
			const auto old_key = std::string("crop_") + edge;
			const auto key = std::string("cut_") + edge;
			if (!r.contains(key) && r.contains(old_key)) {
				double bound = numeric(r.at(old_key), (std::string_view(edge) == "right" ||
								       std::string_view(edge) == "bottom")
									      ? 1
									      : 0);
				r[key] =
					100 * ((std::string_view(edge) == "right" || std::string_view(edge) == "bottom")
						       ? 1 - bound
						       : bound);
			}
		}
		if (!r.contains("guided_eps") && r.contains("guided_log_eps"))
			r["guided_eps"] =
				std::pow(10.0, std::clamp(numeric(r.at("guided_log_eps"), -3.30103), -5.0, -2.0));
		alias(r, "external_hint", "hint_mask");
		alias(r, "fps", "target_fps");
		if (string(r, "hint_mode") == "carry")
			r["hint_mode"] = std::string("previous");
		if (string(r, "obs_view") == "composite")
			r["obs_view"] = std::string("key");
		// Runner inspection preferences must not silently replace normal stream output.
		if (r.contains("view_mode")) {
			r["view_mode"] = std::string("transparent");
			notice("Migration.Inspection");
		}
		if (r.contains("auto_audio_source") && string(r, "auto_audio_source", "none") != "none")
			notice("Migration.Audio");
		if (r.contains("use_cradio") && std::get_if<bool>(&r.at("use_cradio")) &&
		    std::get<bool>(r.at("use_cradio")))
			notice("Migration.Cradio");
		alias(r, "refiner", "refiner_model");
		alias(r, "refiner_path", "refiner_model");
		auto refiner = string(r, "refiner_model");
		for (const auto *id : {"v12", "v10w", "v9b", "v4"})
			if (refiner.find(id) != std::string::npos) {
				r["refiner_model"] = std::string(id);
				break;
			}
		if (string(r, "refiner_model").find_first_of("/\\.") != std::string::npos) {
			r["refiner_model"] = std::string("v12");
			notice("Migration.Refiner");
		}
		// Lua numbers are numeric in obs_data, while native list values are strings.
		for (const auto *key : {"stage2_interval", "target_fps", "guided_scale"})
			if (r.contains(key) && !std::holds_alternative<std::string>(r.at(key)))
				r[key] = number_string(numeric(r.at(key), 0));
	}
	Profile p = Profile::balanced;
	const auto profile = string(r, "profile");
	if (profile == "facecam-60")
		p = Profile::facecam;
	else if (profile == "quality")
		p = Profile::quality;
	else if (profile == "custom")
		p = Profile::custom;
	auto s = defaults(p, string(r, "model_id") == "ck1" ? ModelFamily::ck1 : ModelFamily::ck2);
	for (const auto &f : fields)
		if (auto it = r.find(f.key); it != r.end())
			assign(s, f, it->second);
	if (r.contains("refiner_max_tiles") && numeric(r.at("refiner_max_tiles"), 64) == 0)
		notice("Migration.Tiles");
	clamp(s);
	return s;
}
void edit(Settings &s, std::string_view key, const Value &v)
{
	const auto *f = find(key);
	if (!f)
		return;
	assign(s, *f, v);
	if (key == "refiner_enabled")
		s.quality.guide_mode = s.quality.refiner_enabled ? GuideMode::refiner : GuideMode::rgb_guided;
	if (key == "guide_mode")
		s.quality.refiner_enabled = s.quality.guide_mode == GuideMode::refiner;
	if (std::string_view(f->group) != "crop" && std::string_view(f->group) != "sync" &&
	    std::string_view(f->group) != "output" && key != "model_revision")
		s.quality.profile = Profile::custom;
	clamp(s);
	++s.revision;
}
void Editor::select_model(ModelFamily family)
{
	if (family == current.quality.model)
		return;
	saved[current.quality.model] = current;
	const auto context = current.context;
	const auto revision = current.revision;
	auto it = saved.find(family);
	current = it == saved.end() ? defaults(Profile::balanced, family) : it->second;
	current.context = context;
	current.revision = revision + 1;
}
void Editor::select_preset(Profile p)
{
	if (p == Profile::custom) {
		current.quality.profile = p;
		return;
	}
	select_model(ModelFamily::ck2);
	apply_preset(current, p);
}
} // namespace corridorkey::settings
