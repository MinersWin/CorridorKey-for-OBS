// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/settings.hpp"
#include <cstdint>
#include <string_view>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace corridorkey::settings {
inline constexpr std::int64_t schema_version = 2;
using Value = std::variant<bool, std::int64_t, double, std::string>;
using Record = std::map<std::string, Value, std::less<>>;
enum class Kind { boolean, integer, number, text, choice, color };
enum class Level { normal, advanced, expert, internal };
enum class Disposition { active, derived, experimental, retired };
struct Choice {
	const char *value;
	const char *label;
};
struct Field {
	const char *key;
	const char *group;
	Kind kind;
	Level level;
	Disposition disposition;
	double minimum, maximum, step;
	const char *suffix;
	const char *dependency;
	std::span<const Choice> choices;
	Value (*get)(const Settings &);
	void (*set)(Settings &, const Value &);
};
std::span<const Field> inventory();
const Field *find(std::string_view key);
Settings defaults(Profile profile = Profile::balanced, ModelFamily model = ModelFamily::ck2);
void apply_preset(Settings &, Profile);
void clamp(Settings &);
Settings decode(Record values, std::vector<std::string> *notices = nullptr);
Record encode(const Settings &);
void edit(Settings &, std::string_view key, const Value &);
// Context is shared between pipeline banks. Quality, backend, hints and experiments are independent.
struct Editor {
	Settings current;
	std::map<ModelFamily, Settings> saved;
	void select_model(ModelFamily);
	void select_preset(Profile);
};
struct LegacyField {
	const char *key;
	Disposition disposition;
	const char *replacement;
};
std::span<const LegacyField> legacy_inventory();
} // namespace corridorkey::settings
