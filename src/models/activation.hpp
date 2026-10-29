// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "manifest.hpp"
#include "contracts/settings.hpp"

namespace corridorkey::models {
// The grid selects the package; Quality additionally requires Stage 2.
const Manifest *activation_manifest(const std::vector<Manifest> &, const Settings &);
const Manifest *activation_refiner_manifest(const std::vector<Manifest> &, const Settings &);
// Combines verified packages while keeping both revisions leased through session teardown.
std::shared_ptr<const ReadyModel> activation_model(std::shared_ptr<const ReadyModel>,
						   std::shared_ptr<const ReadyModel>);
} // namespace corridorkey::models
