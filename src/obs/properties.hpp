// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <obs.h>

namespace corridorkey {
class FilterControl;
obs_properties_t *create_filter_properties(FilterControl *control);
} // namespace corridorkey
