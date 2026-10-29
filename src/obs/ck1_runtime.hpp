// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine/ck1_ort.hpp"
#include "properties.hpp"
#include "models/catalog.hpp"

namespace corridorkey {
std::unique_ptr<ModelPipeline> make_model_pipeline(ModelFamily, const ck2::RuntimeConfig &, std::function<TimePoint()>);
void add_model_capabilities(PropertyCapabilities &);
std::shared_ptr<const ReadyModel> acquire_hint_model(std::shared_ptr<const ReadyModel>,
						     const std::vector<models::Manifest> &, const Settings &,
						     models::Cache &, const models::LicenceRegistry &,
						     std::shared_ptr<const ReadyModel> &cached_hint, std::stop_token);
} // namespace corridorkey
