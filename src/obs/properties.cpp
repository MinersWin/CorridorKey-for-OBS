// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "properties.hpp"
#ifdef CORRIDORKEY_MODEL_MANAGER
#include "ui/model_manager.hpp"
#endif
#include "filter_control.hpp"
#include "settings/obs_settings.hpp"
#include <obs-module.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string_view>
#include <tuple>
#ifdef CORRIDORKEY_QT_STATUS
#include <QApplication>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include "telemetry/obs_clock.hpp"
#endif

namespace corridorkey {
namespace {
using namespace settings;
const char *tr(const std::string &key)
{
	return obs_module_text(key.c_str());
}
struct Form {
	FilterControl *control;
	PropertyCapabilities capabilities;
	obs_data_t *previous = nullptr;
	obs_data_t *bound = nullptr;
#ifdef CORRIDORKEY_QT_STATUS
	QPointer<QTimer> timer;
	obs_weak_source_t *owner = nullptr;
	std::uint64_t edited_at = 0;
#endif
	~Form()
	{
#ifdef CORRIDORKEY_QT_STATUS
		if (timer) {
			timer->stop();
			delete timer;
		}
		if (owner)
			obs_weak_source_release(owner);
#endif
		obs_data_release(previous);
		obs_data_release(bound);
	}
};
obs_property_t *property(obs_properties_t *p, const char *name)
{
	return obs_properties_get(p, name);
}
void visible(obs_properties_t *p, const char *name, bool value)
{
	if (auto *f = property(p, name))
		obs_property_set_visible(f, value);
}
void enabled(obs_properties_t *p, const char *name, bool value)
{
	if (auto *f = property(p, name))
		obs_property_set_enabled(f, value);
}
void info(obs_properties_t *p, const char *name, const std::string &text)
{
	if (auto *f = property(p, name)) {
		obs_property_set_description(f, std::string_view(name) == "filter_delay"    ? tr("Delay.Label")
						: std::string_view(name) == "applied_delay" ? tr("Applied.Label")
											    : "");
		auto *root = p;
		while (auto *parent = obs_properties_get_parent(root))
			root = parent;
		auto *form = static_cast<Form *>(obs_properties_get_param(root));
		if (form && form->bound) {
			std::string value;
			for (const char ch : text) {
				if (ch == '\n')
					value += "<br>";
				else if (ch == '&')
					value += "&amp;";
				else if (ch == '<')
					value += "&lt;";
				else if (ch == '>')
					value += "&gt;";
				else
					value += ch;
			}
			obs_data_set_string(form->bound, name, value.c_str());
		} else
			obs_property_set_long_description(f, text.c_str());
	}
}
void status(obs_properties_t *p, Form &form, const Settings &s)
{
	auto state = form.control ? form.control->status_snapshot() : StatusSnapshot{};
	std::string first = tr("Status.Missing"), second;
	if (state) {
		if (state->failure.code == ErrorCode::model_missing) {
			first = tr("Status.Missing");
			second.clear();
		} else if (state->failure) {
			first = tr("Error." + std::string(error_id(state->failure.code)));
			second.clear();
		} else if (state->lifecycle == Lifecycle::ready) {
			first = s.quality.model == ModelFamily::ck2 ? "CK2" : "CK1";
			const char *backend = "auto";
			switch (state->backend) {
			case Backend::tensorrt_rtx:
				backend = "tensorrt_rtx";
				break;
			case Backend::tensorrt:
				backend = "tensorrt";
				break;
			case Backend::cuda:
				backend = "cuda";
				break;
			case Backend::directml:
				backend = "directml";
				break;
			case Backend::migraphx:
				backend = "migraphx";
				break;
			case Backend::cpu_diagnostic:
				backend = "cpu_diagnostic";
				break;
			case Backend::fake:
				backend = "synthetic";
				break;
			default:
				break;
			}
			first += " · ";
			first += tr(std::string("Choice.") + backend);
			if (state->transport == TransportKind::interop)
				first += " \u00b7 " + std::string(tr("Status.DeviceTransport"));
			else if (state->transport == TransportKind::staged_grid)
				first += " \u00b7 " + std::string(tr("Status.StagedTransport"));
			if (state->inference_median_ms && state->age_median_ms && state->keyed_fps) {
				char metrics[256];
				std::snprintf(metrics, sizeof(metrics), tr("Status.Metrics"), *state->keyed_fps,
					      *state->inference_median_ms, *state->age_median_ms);
				second = metrics;
			} else
				second = tr("Status.Warming");
			if (state->untested_combination) {
				first += " · ";
				first += tr("Status.Untested");
			}
		} else if (state->lifecycle == Lifecycle::preparing) {
			first = tr("Status.Preparing");
			second = tr("Status.Wait");
		} else if (state->lifecycle == Lifecycle::warming) {
			first = tr("Status.Warming");
			second = tr("Status.Wait");
		}
	}
	info(p, "status", first + (second.empty() ? "" : " · " + second));
	std::string details = tr("Status.Help");
	if (state) {
		if (state->failure)
			details += "\n" + std::string(error_id(state->failure.code)) + "\n" + state->diagnostic_detail;
		if (state->age_p95_ms)
			details += "\np95: " + std::to_string(*state->age_p95_ms) + " ms";
		details += "\n" + std::string(tr("Status.Dropped")) + ": " + std::to_string(state->dropped);
		details += "\n" + std::string(tr("Status.Repeated")) + ": " + std::to_string(state->repeated);
	}
	obs_property_set_long_description(property(p, "status"), details.c_str());
	info(p, "filter_delay",
	     state && state->age_median_ms ? std::to_string(*state->age_median_ms) + " ms" : tr("Status.NotMeasured"));
	std::string applied = state ? tr("Sync." + state->audio_sync_state) : tr("Sync.off");
	if (state && state->audio_sync_offset_ms) {
		char value[96];
		std::snprintf(value, sizeof(value), tr("Sync.Value"), *state->audio_sync_offset_ms);
		applied = std::string(value) + " \u00b7 " + applied;
	}
	info(p, "applied_delay", applied);
}
bool allowed(Form &form, std::string_view key, std::string_view value)
{
	if (value == "auto")
		return true;
	auto it = form.capabilities.choices.find(key);
	return it != form.capabilities.choices.end() && std::ranges::find(it->second, value) != it->second.end();
}
void visibility(obs_properties_t *p, Form &form, obs_data_t *data)
{
	const auto s = decode(read_record(data));
	const bool advanced = obs_data_get_bool(data, "advanced");
	const bool expert = advanced && obs_data_get_bool(data, "expert");
	const bool ck2 = s.quality.model == ModelFamily::ck2;
	visible(p, "expert", advanced);
	for (const auto &f : inventory()) {
		if (f.level == Level::internal)
			continue;
		const char *name = std::string_view(f.key) == "guided_eps" ? "guided_log_eps" : f.key;
		bool show = f.level == Level::normal || (f.level == Level::advanced && advanced) ||
			    (f.level == Level::expert && expert);
		const std::string_view dep = f.dependency;
		if (dep == "ck2")
			show &= ck2;
		if (dep == "ck1")
			show &= !ck2;
		if (dep == "roi")
			show &= ck2 && s.quality.roi_enabled;
		if (dep == "refiner")
			show &= s.quality.refiner_enabled;
		if (dep == "chroma")
			show &= !ck2 && s.hint.source == HintSource::chroma;
		if (dep == "sync")
			show &= s.context.auto_sync;
		if (dep == "mask")
			show &= expert && ((!ck2 && s.hint.source == HintSource::external_mask) ||
					   (ck2 && s.hint.ck2_mode == HintMode::external));
		if (dep == "head_on")
			show &= s.experimental.head_pass;
		if (std::string_view(f.group) == "crop")
			show &= obs_data_get_bool(data, "details_crop");
		if (std::string_view(f.group) == "output")
			show &= obs_data_get_bool(data, "details_output");
		if (std::string_view(f.group) == "sync")
			show &= obs_data_get_bool(data, "details_sync");
		visible(p, name, show);
		bool available = true;
		if (dep == "ck1_refiner")
			available = ck2 || form.capabilities.ck1_refiner;
		if (dep == "head" || dep == "head_on")
			available = form.capabilities.head_pass;
		if (dep == "stagger")
			available = form.capabilities.stagger;
		if (dep == "mask")
			available = form.capabilities.external_mask;
		if (std::string_view(f.key) == "guided_scale")
			available = form.capabilities.guided_scale;
		if (std::string_view(f.key) == "auto_sync")
			available = form.capabilities.audio_sync;
		enabled(p, name, available);
		std::string help = tr(std::string(f.key) + ".Help");
		if (!available)
			help += "\n" + std::string(tr("Availability.Help"));
		obs_property_set_long_description(property(p, name), help.c_str());
		if (f.kind == Kind::text) {
			auto *list = property(p, name);
			const auto selected = std::get<std::string>(f.get(s));
			bool found = selected.empty();
			for (size_t i = 0; i < obs_property_list_item_count(list); ++i)
				found |= selected == obs_property_list_item_string(list, i);
			if (!found) {
				const auto index = obs_property_list_add_string(list, tr("Choice.saved_unavailable"),
										selected.c_str());
				obs_property_list_item_disable(list, index, true);
				help += "\n" + std::string(tr("Availability.Help"));
				obs_property_set_long_description(list, help.c_str());
			}
		}
		if (f.kind == Kind::choice) {
			auto *list = property(p, name);
			const auto selected = std::get<std::string>(f.get(s));
			obs_property_list_clear(list);
			for (const auto &choice : f.choices) {
				const std::string_view key = f.key, value = choice.value;
				const bool selected_value = selected == value;
				if (key == "hint_source" &&
				    (value == "distilled" || value == "mediapipe" || value == "external_mask") &&
				    !allowed(form, key, value))
					continue;
				if (key == "precision" && value == "bf16" && !allowed(form, key, value) &&
				    !selected_value)
					continue;
				if (key == "stage2_interval" && !expert && value != "0" && value != "1" &&
				    !selected_value)
					continue;
				if (key == "guide_mode" && value == "chroma_legacy" && !expert && !selected_value)
					continue;
				if (key == "obs_view" && !advanced &&
				    (value == "input" || value == "tiles" || value == "roi") && !selected_value)
					continue;
				if (key == "grid" && !selected_value) {
					if (ck2 && (value == "256x256" || value == "384x384"))
						continue;
					if (!ck2 && value != "256x256" && value != "384x384" && value != "512x512")
						continue;
				}
				const char *label = choice.label;
				if (key == "stage2_interval" && value == "0")
					label = "Choice.off";
				if (key == "guided_scale" && value == "1")
					label = "Choice.full_size";
				obs_property_list_add_string(list, tr(label), choice.value);
			}
			for (size_t i = 0; i < obs_property_list_item_count(list); ++i) {
				const std::string_view v = obs_property_list_item_string(list, i);
				bool disabled = false;
				const std::string_view key = f.key;
				if (key == "backend" || key == "precision" || key == "transport")
					disabled = !allowed(form, key, v);
				if (key == "grid")
					disabled = !allowed(form, ck2 ? "ck2.grid" : "ck1.grid", v);
				if (key == "grid") {
					const bool square = v == "256x256" || v == "384x384" || v == "512x512";
					disabled |= !ck2 && !square;
					disabled |= ck2 && (v == "256x256" || v == "384x384");
				}
				if (key == "stage2_interval")
					disabled = !expert && v != "0" && v != "1";
				if (key == "guide_mode")
					disabled = (v == "chroma_legacy" && (!expert || !allowed(form, key, v))) ||
						   (v == "refiner" && !ck2 && !form.capabilities.ck1_refiner);
				if (key == "hint_source")
					disabled = v != "chroma" && !allowed(form, key, v);
				if (key == "obs_view")
					disabled = !advanced && (v == "input" || v == "tiles" || v == "roi");
				obs_property_list_item_disable(list, i, disabled);
			}
		}
	}
	visible(p, "group_performance", advanced);
	visible(p, "group_expert", expert);
	visible(p, "group_hint", advanced && (!ck2 || expert));
	for (const auto *group : {"group_edge", "group_color", "group_output", "group_sync"})
		visible(p, group, advanced);
	visible(p, "reset_memory", advanced);
	visible(p, "reset_quality", advanced);
	visible(p, "diagnostics", advanced);
	visible(p, "refiner_tile", expert && s.quality.refiner_enabled);
	visible(p, "crop_preview", obs_data_get_bool(data, "details_crop"));
	visible(p, "filter_delay", obs_data_get_bool(data, "details_sync"));
	visible(p, "applied_delay", obs_data_get_bool(data, "details_sync"));
	const auto *notice = obs_data_get_string(data, "migration_notice");
	info(p, "migration_info", notice);
	visible(p, "migration_info", *notice != 0);
	info(p, "refiner_tile", tr("Tile.Info"));
	info(p, "applied_delay", tr("Status.NotMeasured"));
	status(p, form, s);
}
bool modified(void *opaque, obs_properties_t *p, obs_property_t *prop, obs_data_t *data)
{
	try {
		auto &form = *static_cast<Form *>(opaque);
#ifdef CORRIDORKEY_QT_STATUS
		form.edited_at = os_gettime_ns();
#endif
		const std::string key = obs_property_name(prop);
		bool rebuild = !form.previous;
		const std::string old_profile = form.previous ? obs_data_get_string(form.previous, "profile") : "";
		// OBS invokes every callback while building a form. Compare with a private copy
		// so opening properties cannot reapply a preset or turn it into Custom.
		if (form.previous) {
			const auto before = read_record(form.previous), after = read_record(data);
			const auto old = before.find(key), now = after.find(key);
			bool changed = (old == before.end()) != (now == after.end());
			if (old != before.end() && now != after.end())
				changed = old->second != now->second;
			if (changed && (find(key) || key == "guided_log_eps"))
				change_property(data, key);
		} else {
			auto editor = read_editor(data);
			write_editor(data, editor);
			obs_data_set_bool(data, "crop_preview", false);
		}
		if (form.bound != data) {
			obs_data_release(form.bound);
			form.bound = data;
			obs_data_addref(data);
		}
		obs_data_release(form.previous);
		form.previous = obs_data_create();
		obs_data_apply(form.previous, data);
		visibility(p, form, data);
		const auto *field = find(key);
		rebuild |= !field || field->kind == Kind::boolean || field->kind == Kind::choice;
		rebuild |= old_profile != obs_data_get_string(data, "profile");
		return rebuild;
	} catch (...) {
		blog(LOG_ERROR, "[corridorkey] property update failed");
		return false;
	}
}
bool button(obs_properties_t *p, obs_property_t *prop, void *opaque)
{
	auto &form = *static_cast<Form *>(opaque);
	const std::string_view key = obs_property_name(prop);
	try {
		if (key == "reset_memory") {
			if (form.control)
				form.control->request_reset();
		} else if (key == "manage_models") {
#ifdef CORRIDORKEY_MODEL_MANAGER
			ui::open_model_manager();
			visible(p, "action_notice", false);
			return false;
#else
			info(p, "action_notice", tr("Models.Placeholder"));
#endif
		} else if (key == "diagnostics")
			info(p, "action_notice", tr("Diagnostics.Placeholder"));
		visible(p, "action_notice", key != "reset_memory");
		return true;
	} catch (...) {
		return false;
	}
}
bool reset_quality(obs_properties_t *p, obs_property_t *, void *owner)
{
	while (auto *parent = obs_properties_get_parent(p))
		p = parent;
	auto &form = *static_cast<Form *>(obs_properties_get_param(p));
	if (!form.bound)
		return false;
	try {
		auto editor = read_editor(form.bound);
		editor.select_preset(editor.current.quality.profile == Profile::custom
					     ? Profile::balanced
					     : editor.current.quality.profile);
		write_editor(form.bound, editor);
		if (owner)
			obs_source_update(static_cast<obs_source_t *>(owner), form.bound);
		obs_data_release(form.previous);
		form.previous = obs_data_create();
		obs_data_apply(form.previous, form.bound);
		visibility(p, form, form.bound);
		return true;
	} catch (...) {
		return false;
	}
}
obs_property_t *add_field(obs_properties_t *p, const Field &f, Form *form)
{
	const auto label = std::string(f.key) + ".Label";
	obs_property_t *prop = nullptr;
	if (std::string_view(f.key) == "refiner_max_tiles" || std::string_view(f.key) == "head_max_tiles" ||
	    std::string_view(f.key) == "head_size") {
		prop = obs_properties_add_list(p, f.key, tr(label), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (int value = static_cast<int>(f.minimum); value <= static_cast<int>(f.maximum);
		     value += static_cast<int>(f.step)) {
			const auto text = std::to_string(value);
			const auto index = obs_property_list_add_int(prop, text.c_str(), value);
			obs_property_list_item_disable(prop, index, !allowed(*form, f.key, text));
		}
	} else if (std::string_view(f.key) == "guided_eps") {
		prop = obs_properties_add_float_slider(p, "guided_log_eps", tr(label), -5, -2, .1);
	} else if (f.kind == Kind::boolean)
		prop = obs_properties_add_bool(p, f.key, tr(label));
	else if (f.kind == Kind::integer) {
		prop = obs_properties_add_int_slider(p, f.key, tr(label), static_cast<int>(f.minimum),
						     static_cast<int>(f.maximum), static_cast<int>(f.step));
		obs_property_int_set_suffix(prop, f.suffix);
	} else if (f.kind == Kind::number) {
		prop = obs_properties_add_float_slider(p, f.key, tr(label), f.minimum, f.maximum, f.step);
		obs_property_float_set_suffix(prop, f.suffix);
	} else if (f.kind == Kind::color)
		prop = obs_properties_add_color(p, f.key, tr(label));
	else {
		prop = obs_properties_add_list(p, f.key, tr(label), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		if (f.kind == Kind::choice) {
			for (const auto &choice : f.choices) {
				if (std::string_view(f.key) == "hint_source" &&
				    (std::string_view(choice.value) == "distilled" ||
				     std::string_view(choice.value) == "mediapipe" ||
				     std::string_view(choice.value) == "external_mask") &&
				    !allowed(*form, f.key, choice.value))
					continue;
				const char *choice_label = choice.label;
				if (std::string_view(f.key) == "stage2_interval" &&
				    std::string_view(choice.value) == "0")
					choice_label = "Choice.off";
				if (std::string_view(f.key) == "guided_scale" && std::string_view(choice.value) == "1")
					choice_label = "Choice.full_size";
				obs_property_list_add_string(prop, tr(choice_label), choice.value);
			}
		} else if (std::string_view(f.key) == "refiner_model") {
			for (const auto *id : {"v12", "v10w", "v9b", "v4"}) {
				if (std::string_view(id) == "v4" && !allowed(*form, f.key, id))
					continue;
				const auto index = obs_property_list_add_string(prop, id, id);
				obs_property_list_item_disable(prop, index, !allowed(*form, f.key, id));
			}
		} else {
			obs_property_list_add_string(
				prop, tr(std::string_view(f.key) == "device" ? "Choice.auto" : "Choice.none"), "");
			if (std::string_view(f.key) == "audio_source_id") {
				obs_enum_sources(
					[](void *list, obs_source_t *source) {
						if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)
							obs_property_list_add_string(
								static_cast<obs_property_t *>(list),
								obs_source_get_name(source),
								obs_source_get_uuid(source));
						return true;
					},
					prop);
			} else if (auto it = form->capabilities.choices.find(f.key);
				   it != form->capabilities.choices.end())
				for (const auto &value : it->second)
					obs_property_list_add_string(prop, value.c_str(), value.c_str());
		}
	}
	obs_property_set_long_description(prop, tr(std::string(f.key) + ".Help"));
	obs_property_set_modified_callback2(prop, modified, form);
	return prop;
}
void add_info(obs_properties_t *p, const char *key, const char *text, const char *help)
{
	auto *prop = obs_properties_add_text(p, key, "", OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(prop, true);
	obs_property_set_long_description(prop, tr(help));
	(void)text;
}
} // namespace
obs_properties_t *create_filter_properties(FilterControl *control, PropertyCapabilities capabilities)
{
	auto form = std::make_unique<Form>();
	form->control = control;
	form->capabilities = std::move(capabilities);
	auto *state = form.get();
	auto *p = obs_properties_create_param(form.release(), [](void *v) { delete static_cast<Form *>(v); });
	add_field(p, *find("profile"), state);
	add_field(p, *find("model_id"), state);
	add_field(p, *find("refiner_enabled"), state);
	add_info(p, "status", "Status.Missing", "Status.Help");
	auto *manager = obs_properties_add_button2(p, "manage_models", tr("Models.Manage"), button, state);
	obs_property_set_long_description(manager, tr("Models.Help"));
	add_info(p, "action_notice", "Models.Placeholder", "Models.Help");
	visible(p, "action_notice", false);
	add_info(p, "migration_info", "Status.Setup", "Migration.Help");
	visible(p, "migration_info", false);
	for (const auto *group : {"crop", "performance", "edge", "color", "hint", "output", "sync", "expert"}) {
		auto *children = obs_properties_create();
		const std::string g = group;
		if (g == "crop" || g == "output" || g == "sync") {
			auto *details =
				obs_properties_add_bool(children, ("details_" + g).c_str(), tr("Details.Label"));
			obs_property_set_long_description(details, tr("Details.Help"));
			obs_property_set_modified_callback2(details, modified, state);
		}
		for (const auto &f : inventory())
			if (f.level != Level::internal && g == f.group && std::string_view(f.key) != "refiner_enabled")
				add_field(children, f, state);
		if (g == "crop") {
			auto *preview = obs_properties_add_bool(children, "crop_preview", tr("CropPreview.Label"));
			obs_property_set_long_description(preview, tr("CropPreview.Help"));
			obs_property_set_enabled(preview, false);
		}
		if (g == "sync") {
			add_info(children, "filter_delay", "Status.NotMeasured", "Delay.Help");
			add_info(children, "applied_delay", "Status.NotMeasured", "Applied.Help");
		}
		if (g == "expert")
			add_info(children, "refiner_tile", "Tile.Info", "Tile.Help");
		auto *heading = obs_properties_add_group(p, ("group_" + g).c_str(), tr("Group." + g), OBS_GROUP_NORMAL,
							 children);
		obs_property_set_long_description(heading, tr("Group." + g + ".Help"));
		if (g == "crop") {
			auto *advanced = obs_properties_add_bool(p, "advanced", tr("Advanced.Label"));
			obs_property_set_long_description(advanced, tr("Advanced.Help"));
			obs_property_set_modified_callback2(advanced, modified, state);
			auto *expert = obs_properties_add_bool(p, "expert", tr("Expert.Label"));
			obs_property_set_long_description(expert, tr("Expert.Help"));
			obs_property_set_modified_callback2(expert, modified, state);
		}
	}
	for (const auto &[key, label, help] : {std::tuple{"reset_memory", "ResetMemory.Label", "ResetMemory.Help"},
					       std::tuple{"reset_quality", "ResetQuality.Label", "ResetQuality.Help"},
					       std::tuple{"diagnostics", "Diagnostics.Label", "Diagnostics.Help"}}) {
		// reset_quality needs the source that OBS passes to plain button callbacks, and
		// obs_properties_add_button2 replaces that argument with priv. OBS 33 deprecates
		// obs_properties_add_button, but it still works across the supported 30.2+ range.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		auto *action = std::string_view(key) == "reset_quality"
				       ? obs_properties_add_button(p, key, tr(label), reset_quality)
				       : obs_properties_add_button2(p, key, tr(label), button, state);
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
		obs_property_set_long_description(action, tr(help));
	}
	auto *initial = obs_data_create();
	set_filter_defaults(initial);
	visibility(p, *state, initial);
	obs_data_release(initial);
#ifdef CORRIDORKEY_QT_STATUS
	if (qApp && QThread::currentThread() == qApp->thread() && control && control->property_source()) {
		state->owner = obs_source_get_weak_source(control->property_source());
		state->timer = new QTimer(qApp);
		state->timer->setInterval(2000);
		QObject::connect(state->timer, &QTimer::timeout, state->timer, [state, p] {
			if (!state->bound || os_gettime_ns() - state->edited_at < 750000000 ||
			    QApplication::mouseButtons() != Qt::NoButton || QApplication::activePopupWidget())
				return;
			const auto *focus = QApplication::focusWidget();
			if (focus && (focus->inherits("QAbstractSpinBox") || focus->inherits("QAbstractSlider") ||
				      focus->inherits("QLineEdit") || focus->inherits("QComboBox")))
				return;
			auto *source = obs_weak_source_get_source(state->owner);
			if (!source)
				return;
			try {
				status(p, *state, decode(read_record(state->bound)));
				// OBS owns the property widgets; refresh through its supported public signal.
				obs_source_update_properties(source);
			} catch (...) {
			}
			obs_source_release(source);
		});
		state->timer->start();
	}
#endif
	return p;
}
} // namespace corridorkey
