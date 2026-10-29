// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation.hpp"

namespace corridorkey::models {
const Manifest *activation_manifest(const std::vector<Manifest> &manifests, const Settings &settings)
{
	const auto grid = settings.quality.grid;
	if (settings.quality.model == ModelFamily::ck1) {
		const auto id = settings.quality.screen_color == ScreenColor::blue ? "ck1-blue" : "ck1-green";
		for (const auto &m : manifests)
			if (m.pipeline == "ck1" && m.key.id == id &&
			    (settings.quality.model_revision.empty() ||
			     settings.quality.model_revision == m.key.revision))
				for (const auto &profile : m.profiles)
					if (profile.at("grid_hw") == Json::array({grid.height, grid.width}))
						return &m;
		return nullptr;
	}
	const bool quality = settings.quality.profile == Profile::quality;
	for (const auto &m : manifests) {
		if (m.pipeline != "ck2" ||
		    (!settings.quality.model_revision.empty() && settings.quality.model_revision != m.key.revision))
			continue;
		for (const auto &profile : m.profiles) {
			if (profile.at("grid_hw") != Json::array({grid.height, grid.width}))
				continue;
			bool stage2 = false;
			for (const auto &id : profile.at("graphs"))
				for (const auto &g : m.graphs)
					if (g.id == string(id) && g.role == "stage2")
						stage2 = true;
			if (quality == stage2)
				return &m;
		}
	}
	return nullptr;
}
const Manifest *activation_hint_manifest(const std::vector<Manifest> &manifests, const Settings &settings)
{
	if (settings.quality.model != ModelFamily::ck1 || settings.hint.source == HintSource::chroma)
		return nullptr;
	if (settings.hint.source == HintSource::ck2_stage1) {
		auto hint = settings;
		hint.quality.model = ModelFamily::ck2;
		hint.quality.model_revision.clear();
		hint.quality.grid = {512, 512};
		hint.quality.profile = Profile::custom;
		return activation_manifest(manifests, hint);
	}
	if (settings.hint.source == HintSource::modnet)
		for (const auto &m : manifests)
			if (m.pipeline == "hint" && m.key.id == "modnet")
				for (const auto &profile : m.profiles)
					if (profile.at("grid_hw") ==
					    Json::array({settings.quality.grid.height, settings.quality.grid.width}))
						return &m;
	return nullptr;
}
const Manifest *activation_refiner_manifest(const std::vector<Manifest> &manifests, const Settings &settings)
{
	if (settings.quality.model != ModelFamily::ck2 || !settings.quality.refiner_enabled)
		return nullptr;
	const auto graph = "refiner_b" + std::to_string(settings.quality.refiner_max_tiles);
	for (const auto &m : manifests)
		for (const auto &g : m.graphs)
			if (g.role == "refiner" && g.id == graph)
				return &m;
	return nullptr;
}
std::shared_ptr<const ReadyModel> activation_model(std::shared_ptr<const ReadyModel> base,
						   std::shared_ptr<const ReadyModel> refinement)
{
	require(base && base->lease && refinement && refinement->lease, "Unleased activation package");
	struct Leases final : ModelReadLease {
		std::shared_ptr<const ReadyModel> base, refinement;
	};
	auto leases = std::make_shared<Leases>();
	leases->base = base;
	leases->refinement = refinement;
	auto combined = std::make_shared<ReadyModel>(*base);
	combined->lease = std::move(leases);
	combined->manifest_sha256 = sha256(base->manifest_sha256 + ":" + refinement->manifest_sha256);
	combined->graphs.insert(combined->graphs.end(), refinement->graphs.begin(), refinement->graphs.end());
	return combined;
}
} // namespace corridorkey::models
