// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog.hpp"
#include "engine/providers/runtime.hpp"

namespace corridorkey::models {
// Call from a control/loader thread. Paths come exclusively from OBS's module/config APIs.
std::vector<Manifest> installed_catalogue();
ort::ProbeConfig installed_probe_config();
} // namespace corridorkey::models
