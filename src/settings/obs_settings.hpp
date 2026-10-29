// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "schema.hpp"
#include <obs.h>

namespace corridorkey {
void set_filter_defaults(obs_data_t *settings);
SettingsSnapshot read_settings(obs_data_t *settings);
namespace settings {
Record read_record(obs_data_t *);
void write_record(obs_data_t *, const Record &, bool defaults = false);
Editor read_editor(obs_data_t *);
void write_editor(obs_data_t *, const Editor &);
void change_property(obs_data_t *, std::string_view key);
} // namespace settings
} // namespace corridorkey
