// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "settings/obs_settings.hpp"
#include "obs/properties.hpp"
#include "obs/filter_control.hpp"
#include <obs.h>
#include <util/text-lookup.h>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
static lookup_t *locale;
extern "C" const char *obs_module_text(const char *key)
{
	const char *value = key;
	text_lookup_getstr(locale, key, &value);
	return value;
}
static void check(bool ok, const char *message)
{
	if (!ok) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}
struct StatusControl : corridorkey::FilterControl {
	std::shared_ptr<corridorkey::Status> state = std::make_shared<corridorkey::Status>();
	corridorkey::SettingsSnapshot settings_snapshot() const override { return {}; }
	corridorkey::StatusSnapshot status_snapshot() const override { return state; }
	void request_reset() noexcept override {}
};
int main(int argc, char **argv)
{
	check(argc == 2, "locale directory required");
	check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
	for (const auto *language : {"en-US", "de-DE"}) {
		locale = text_lookup_create((std::string(argv[1]) + "/" + language + ".ini").c_str());
		check(locale != nullptr, "locale load");
		auto *data = obs_data_create();
		corridorkey::set_filter_defaults(data);
		auto original = corridorkey::read_settings(data);
		check(original->quality.refiner_max_tiles == 512, "balanced defaults");
		auto *props = corridorkey::create_filter_properties(nullptr);
		obs_properties_apply_settings(props, data);
		check(obs_property_list_item_count(obs_properties_get(props, "profile")) == 4, "four presets");
		check(!obs_property_visible(obs_properties_get(props, "expert")), "Expert hidden initially");
		check(obs_property_visible(obs_properties_get(props, "refiner_enabled")),
		      "Edge refiner stays in standard view");
		for (const auto *key : {"group_edge", "group_color", "group_sync", "group_output"})
			check(!obs_property_visible(obs_properties_get(props, key)),
			      "Detailed sections stay behind Advanced");
		auto *ordered = obs_properties_first(props);
		for (const auto *key : {"profile", "model_id", "refiner_enabled", "status", "manage_models"}) {
			check(std::string(obs_property_name(ordered)) == key, "Everyday property order");
			obs_property_next(&ordered);
		}
		StatusControl control;
		control.state->failure = corridorkey::error(corridorkey::ErrorCode::model_missing);
		control.state->diagnostic_detail = "Developer-only diagnostic marker";
		auto *status_props = corridorkey::create_filter_properties(&control);
		obs_properties_apply_settings(status_props, data);
		const std::string missing = obs_data_get_string(data, "status");
		check(missing.find("CK-E") == std::string::npos && missing.find("Developer-only") == std::string::npos,
		      "Status hides diagnostic identifiers and raw detail");
		check(std::string(obs_property_long_description(obs_properties_get(status_props, "status")))
				      .find("CK-E010") != std::string::npos,
		      "Diagnostic code remains in the tooltip");
		control.state->failure = {};
		control.state->lifecycle = corridorkey::Lifecycle::ready;
		control.state->backend = corridorkey::Backend::directml;
		control.state->keyed_fps = 30.;
		control.state->inference_median_ms = 12.;
		control.state->age_median_ms = 40.;
		obs_properties_apply_settings(status_props, data);
		const std::string ready = obs_data_get_string(data, "status");
		check(ready.find("<br>") == std::string::npos && ready.find("30.0") != std::string::npos &&
			      ready.find("DirectML") != std::string::npos,
		      "Ready status keeps backend, frame rate and delay on one line");
		obs_properties_destroy(status_props);
		check(!obs_property_visible(obs_properties_get(props, "group_performance")),
		      "Advanced group hidden initially");
		obs_data_set_bool(data, "advanced", true);
		obs_property_modified(obs_properties_get(props, "advanced"), data);
		check(obs_property_visible(obs_properties_get(props, "expert")), "Advanced reveals Expert");
		obs_data_set_bool(data, "expert", true);
		obs_property_modified(obs_properties_get(props, "expert"), data);
		check(obs_property_visible(obs_properties_get(props, "moge_interval")), "Expert reveals geometry");
		obs_data_set_double(data, "alpha_contrast", 1.21);
		obs_property_modified(obs_properties_get(props, "alpha_contrast"), data);
		check(std::string(obs_data_get_string(data, "profile")) == "custom", "quality edit selects Custom");
		obs_data_set_double(data, "alpha_contrast", 1.22);
		check(!obs_property_modified(obs_properties_get(props, "alpha_contrast"), data),
		      "slider drag does not keep rebuilding Custom form");
		obs_data_set_bool(data, "details_crop", true);
		obs_property_modified(obs_properties_get(props, "details_crop"), data);
		check(obs_property_visible(obs_properties_get(props, "cut_right")), "native group expansion");
		obs_data_set_double(data, "cut_right", 12.5);
		check(!obs_property_modified(obs_properties_get(props, "cut_right"), data),
		      "crop drag does not rebuild form");
		check(!obs_data_get_bool(data, "crop_preview"), "crop edit never enables stream-visible preview");
		obs_data_set_string(data, "guided_log_eps", "malformed");
		obs_data_set_double(data, "guided_log_eps", -4.0);
		obs_property_modified(obs_properties_get(props, "guided_log_eps"), data);
		check(std::abs(corridorkey::read_settings(data)->quality.guided_epsilon - .0001) < .000001,
		      "log epsilon maps to exact stored epsilon");
		check(obs_property_button_clicked(obs_properties_get(props, "reset_quality"), nullptr), "reset button");
		check(corridorkey::read_settings(data)->quality.profile == corridorkey::Profile::balanced,
		      "Custom reset selects Balanced");
		check(std::abs(corridorkey::read_settings(data)->context.crop.right - .875) < .000001,
		      "reset retains crop");
		obs_data_set_string(data, "profile", "facecam-60");
		obs_property_modified(obs_properties_get(props, "profile"), data);
		check(corridorkey::read_settings(data)->quality.refiner_max_tiles == 384, "Facecam preset applied");
		obs_data_set_string(data, "model_id", "ck1");
		obs_property_modified(obs_properties_get(props, "model_id"), data);
		check(corridorkey::read_settings(data)->quality.grid == corridorkey::Extent{384, 384}, "CK1 defaults");
		check(obs_property_list_item_count(obs_properties_get(props, "hint_source")) == 3,
		      "future hints hidden");
		obs_data_set_string(data, "model_id", "ck2");
		obs_property_modified(obs_properties_get(props, "model_id"), data);
		check(corridorkey::read_settings(data)->quality.grid == corridorkey::Extent{512, 256},
		      "CK2 bank restored");
		auto *roundtrip = obs_data_create_from_json(obs_data_get_json(data));
		check(roundtrip != nullptr, "JSON round trip parse");
		check(corridorkey::settings::encode(*corridorkey::read_settings(roundtrip)) ==
			      corridorkey::settings::encode(*corridorkey::read_settings(data)),
		      "JSON settings round trip");
		obs_properties_destroy(props);
		props = corridorkey::create_filter_properties(nullptr);
		obs_properties_apply_settings(props, data);
		check(corridorkey::read_settings(data)->quality.grid == corridorkey::Extent{512, 256},
		      "reopening properties retains effective Custom and preset values");
		obs_data_set_bool(data, "advanced", true);
		obs_property_modified(obs_properties_get(props, "advanced"), data);
		obs_data_set_bool(data, "expert", true);
		obs_property_modified(obs_properties_get(props, "expert"), data);
		obs_data_set_bool(data, "advanced", false);
		obs_property_modified(obs_properties_get(props, "advanced"), data);
		check(!obs_property_visible(obs_properties_get(props, "moge_interval")),
		      "Expert cannot remain visible without Advanced");
		obs_data_set_int(data, "settings_version", 99);
		obs_data_set_string(data, "future_field", "retained");
		corridorkey::read_settings(data);
		check(obs_data_get_int(data, "settings_version") == 99, "newer schema marker preserved");
		check(std::string(obs_data_get_string(data, "future_field")) == "retained",
		      "unknown future data preserved");
		for (const auto &f : corridorkey::settings::inventory()) {
			const char *label = nullptr, *help = nullptr;
			check(text_lookup_getstr(locale, (std::string(f.key) + ".Label").c_str(), &label),
			      "translated label");
			check(text_lookup_getstr(locale, (std::string(f.key) + ".Help").c_str(), &help),
			      "translated tooltip");
		}
		corridorkey::PropertyCapabilities caps;
		caps.choices["ck2.grid"] = {"256x512", "512x512"};
		caps.choices["ck1.grid"] = {"384x384"};
		caps.choices["refiner_max_tiles"] = {"384", "512"};
		caps.choices["hint_source"] = {"modnet", "distilled"};
		auto *cap_props = corridorkey::create_filter_properties(nullptr, caps);
		obs_properties_apply_settings(cap_props, data);
		check(obs_property_list_item_count(obs_properties_get(cap_props, "hint_source")) == 4,
		      "manifest reveals custom hint slot");
		auto *grids = obs_properties_get(cap_props, "grid");
		for (size_t index = 0; index < obs_property_list_item_count(grids); ++index) {
			const std::string grid = obs_property_list_item_string(grids, index);
			check(obs_property_list_item_disabled(grids, index) == (grid != "256x512" && grid != "512x512"),
			      "only installed family grids enabled");
		}
		auto *tiles = obs_properties_get(cap_props, "refiner_max_tiles");
		for (size_t index = 0; index < obs_property_list_item_count(tiles); ++index) {
			const auto budget = obs_property_list_item_int(tiles, index);
			check(obs_property_list_item_disabled(tiles, index) == (budget != 384 && budget != 512),
			      "only installed tile batches enabled");
		}
		check(!obs_property_enabled(obs_properties_get(cap_props, "head_pass")),
		      "unsupported experiment disabled");
		obs_properties_destroy(cap_props);
		obs_data_release(roundtrip);
		obs_properties_destroy(props);
		obs_data_release(data);
		text_lookup_destroy(locale);
	}
	obs_shutdown();
	std::cout << "Settings, callbacks, visibility, locales and OBS JSON round trips passed.\n";
}
