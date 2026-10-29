// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "obs_settings.hpp"
#include <atomic>
#include <cmath>
#include <obs-module.h>

namespace corridorkey {
namespace settings {
Record read_record(obs_data_t *data)
{
	Record record;
	for (auto *item = obs_data_first(data); item; obs_data_item_next(&item)) {
		if (!obs_data_item_has_user_value(item))
			continue;
		const auto *name = obs_data_item_get_name(item);
		switch (obs_data_item_gettype(item)) {
		case OBS_DATA_STRING:
			record[name] = std::string(obs_data_item_get_string(item));
			break;
		case OBS_DATA_BOOLEAN:
			record[name] = obs_data_item_get_bool(item);
			break;
		case OBS_DATA_NUMBER:
			if (obs_data_item_numtype(item) == OBS_DATA_NUM_INT)
				record[name] = static_cast<std::int64_t>(obs_data_item_get_int(item));
			else
				record[name] = obs_data_item_get_double(item);
			break;
		default:
			break;
		}
	}
	return record;
}
void write_record(obs_data_t *data, const Record &record, bool defaults)
{
	for (const auto &[key, v] : record) {
		const auto *k = key.c_str();
		if (const auto *p = std::get_if<bool>(&v)) {
			if (defaults)
				obs_data_set_default_bool(data, k, *p);
			else
				obs_data_set_bool(data, k, *p);
		} else if (const auto *p = std::get_if<std::int64_t>(&v)) {
			if (defaults)
				obs_data_set_default_int(data, k, *p);
			else
				obs_data_set_int(data, k, *p);
		} else if (const auto *p = std::get_if<double>(&v)) {
			if (defaults)
				obs_data_set_default_double(data, k, *p);
			else
				obs_data_set_double(data, k, *p);
		} else {
			const auto &s = std::get<std::string>(v);
			if (defaults)
				obs_data_set_default_string(data, k, s.c_str());
			else
				obs_data_set_string(data, k, s.c_str());
		}
	}
}
Editor read_editor(obs_data_t *data)
{
	Editor editor;
	std::vector<std::string> notices;
	editor.current = decode(read_record(data), &notices);
	std::string notice;
	for (const auto &key : notices) {
		if (!notice.empty())
			notice += "\n";
		notice += obs_module_text(key.c_str());
	}
	if (!notice.empty())
		obs_data_set_string(data, "migration_notice", notice.c_str());
	for (const auto family : {ModelFamily::ck2, ModelFamily::ck1}) {
		auto *bank = obs_data_get_obj(data, family == ModelFamily::ck2 ? "pipeline_ck2" : "pipeline_ck1");
		if (bank) {
			editor.saved[family] = decode(read_record(bank));
			obs_data_release(bank);
		}
	}
	return editor;
}
void write_editor(obs_data_t *data, const Editor &editor)
{
	const auto saved_version = obs_data_get_int(data, "settings_version");
	write_record(data, encode(editor.current));
	if (saved_version > schema_version)
		obs_data_set_int(data, "settings_version", saved_version);
	obs_data_set_string(data, "active_model", editor.current.quality.model == ModelFamily::ck2 ? "ck2" : "ck1");
	obs_data_set_double(data, "guided_log_eps", std::log10(editor.current.quality.guided_epsilon));
	for (const auto &[family, saved] : editor.saved) {
		const char *key = family == ModelFamily::ck2 ? "pipeline_ck2" : "pipeline_ck1";
		auto *bank = obs_data_get_obj(data, key);
		if (!bank)
			bank = obs_data_create();
		const auto bank_version = obs_data_get_int(bank, "settings_version");
		write_record(bank, encode(saved));
		if (bank_version > schema_version)
			obs_data_set_int(bank, "settings_version", bank_version);
		obs_data_set_obj(data, key, bank);
		obs_data_release(bank);
	}
}
void change_property(obs_data_t *data, std::string_view key)
{
	// Read the old family before applying the newly selected model row.
	const std::string requested_model = obs_data_get_string(data, "model_id");
	const std::string active = obs_data_get_string(data, "active_model");
	if (key == "model_id" && !active.empty() && active != requested_model)
		obs_data_set_string(data, "model_id", active.c_str());
	const auto requested = read_record(data);
	auto editor = read_editor(data);
	if (key == "model_id")
		editor.select_model(requested_model == "ck1" ? ModelFamily::ck1 : ModelFamily::ck2);
	else if (key == "profile")
		editor.select_preset(editor.current.quality.profile);
	else if (key == "guided_log_eps")
		edit(editor.current, "guided_eps", std::pow(10.0, obs_data_get_double(data, "guided_log_eps")));
	else if (find(key) && requested.contains(key))
		edit(editor.current, key, requested.at(std::string(key)));
	write_editor(data, editor);
}
} // namespace settings
void set_filter_defaults(obs_data_t *data)
{
	settings::write_record(data, settings::encode(settings::defaults()), true);
	obs_data_set_default_double(data, "guided_log_eps", std::log10(.0005));
	obs_data_set_default_bool(data, "advanced", false);
	obs_data_set_default_bool(data, "expert", false);
	obs_data_set_default_bool(data, "crop_preview", false);
}
SettingsSnapshot read_settings(obs_data_t *data)
{
	static std::atomic<std::uint64_t> revision{0};
	auto editor = settings::read_editor(data);
	editor.current.revision = revision.fetch_add(1, std::memory_order_relaxed) + 1;
	settings::write_editor(data, editor);
	return std::make_shared<const Settings>(std::move(editor.current));
}
} // namespace corridorkey
