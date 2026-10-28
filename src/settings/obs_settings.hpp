// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/settings.hpp"
#include <obs.h>

namespace corridorkey {
void set_filter_defaults(obs_data_t *settings);
SettingsSnapshot read_settings(obs_data_t *settings);
} // namespace corridorkey
