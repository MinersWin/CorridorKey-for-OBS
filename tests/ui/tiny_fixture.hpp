// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/manager_model.hpp"
#include <fstream>

namespace corridorkey::ui::testing {
inline models::LicenceRegistry licence_registry()
{
	const std::string text = "Synthetic fixture licence. No model data.";
	return models::LicenceRegistry::parse(
		models::Json{{"schema_version", 1},
			     {"licenses", models::Json::array({{{"id", "LicenseRef-Test"},
								{"text", text},
								{"text_sha256", models::sha256(text)},
								{"source_url", "https://example.invalid/license"},
								{"attribution", "Test fixture authors"},
								{"scope", "Synthetic bytes"}}})}}
			.dump());
}
inline models::Manifest manifest(const models::fs::path &folder)
{
	auto raw = models::read_small_file(folder / "manifest.json");
	return models::parse_manifest(raw, models::sha256(raw));
}
inline Binder binder(std::chrono::milliseconds delay = {})
{
	return [delay](const models::Manifest &m, models::Cache &cache, std::stop_token stop,
		       const std::function<void(BindingProgress)> &progress) {
		progress({0, 4});
		const auto until = Clock::now() + delay;
		while (Clock::now() < until && !stop.stop_requested())
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (stop.stop_requested())
			return BindingResult{error(ErrorCode::cancelled)};
		models::atomic_write(cache.staging(m.key) / "graphs/tiny.weights", "1234");
		progress({4, 4});
		return BindingResult{};
	};
}
} // namespace corridorkey::ui::testing
