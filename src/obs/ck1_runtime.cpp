// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck1_runtime.hpp"
#include "engine/hints/chroma_obs.hpp"
#include "models/installation.hpp"
#include "models/activation.hpp"
#include <obs-module.h>
#include <util/bmem.h>

namespace corridorkey {
std::shared_ptr<const ReadyModel>
acquire_hint_model(std::shared_ptr<const ReadyModel> base, const std::vector<models::Manifest> &manifests,
		   const Settings &settings, models::Cache &cache, const models::LicenceRegistry &licences,
		   std::shared_ptr<const ReadyModel> &cached_hint, std::stop_token stop)
{
	if (settings.quality.model != ModelFamily::ck1 || settings.hint.source == HintSource::chroma)
		return base;
	const auto *hint = models::activation_hint_manifest(manifests, settings);
	models::require(hint != nullptr, "No package supports the selected CK1 hint.", ErrorCode::model_missing);
	models::require(models::AcceptanceStore(cache).contains(*hint, licences),
			"Open Manage models and accept the hint package licences.", ErrorCode::licence_required);
	auto model = cached_hint && cached_hint->key == hint->key && cached_hint->manifest_sha256 == hint->digest
			     ? cached_hint
			     : cache.acquire(*hint, stop);
	models::require(bool(model), "Install MODNet hint, or CK2 Balanced for a Stage-1 hint, in Manage models.",
			ErrorCode::model_missing);
	cached_hint = model;
	return models::activation_model(std::move(base), std::move(model));
}
void add_model_capabilities(PropertyCapabilities &capabilities)
{
	models::Cache cache(models::obs_model_cache_path());
	for (const auto &manifest : models::installed_catalogue()) {
		// Publication markers are UI availability hints. Activation rechecks licences, hashes and leases.
		if (!std::filesystem::is_regular_file(cache.revision(manifest.key) / "ready.json"))
			continue;
		if (manifest.pipeline == "ck1" || manifest.pipeline == "ck2")
			for (const auto &profile : manifest.profiles) {
				const auto &grid = profile.at("grid_hw");
				capabilities.choices[manifest.pipeline + ".grid"].push_back(
					std::to_string(grid.at(0).get<unsigned>()) + "x" +
					std::to_string(grid.at(1).get<unsigned>()));
			}
		if (manifest.key.id == "modnet")
			capabilities.choices["hint_source"].push_back("modnet");
		if (manifest.key.id == "ck2-balanced")
			capabilities.choices["hint_source"].push_back("ck2_stage1");
	}
}
std::unique_ptr<ModelPipeline> make_model_pipeline(ModelFamily family, const ck2::RuntimeConfig &runtime,
						   std::function<TimePoint()> now)
{
	if (family == ModelFamily::ck1)
		return std::make_unique<Ck1Pipeline>(
			[runtime](const PrepareRequest &request) {
				ck1::ChromaHint chroma;
				if (request.settings->hint.source == HintSource::chroma) {
					char *path = obs_module_file("effects/ck1_chroma.effect");
					std::unique_ptr<char, decltype(&bfree)> owned(path, bfree);
					chroma = ck1::obs_chroma_hint(request.settings->quality.grid, path);
				}
				return ck1::prepare_ort(request, runtime, std::move(chroma));
			},
			std::move(now));
	return std::make_unique<Ck2Pipeline>(
		[runtime](const PrepareRequest &r) { return ck2::prepare_ort(r, runtime); }, std::move(now));
}
} // namespace corridorkey
