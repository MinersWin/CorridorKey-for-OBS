// SPDX-License-Identifier: GPL-3.0-or-later
#include "installation.hpp"
#include "util/utf8_path.hpp"
#include <obs-module.h>
#include <util/bmem.h>

namespace corridorkey::models {
namespace {
fs::path module_file(const char *relative)
{
	char *raw = obs_module_file(relative);
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	require(raw != nullptr, "Installed CorridorKey data is missing", ErrorCode::io_failure);
	return utf8_path(raw);
}
} // namespace
std::vector<Manifest> installed_catalogue()
{
	Cache cache(obs_model_cache_path());
	const auto path = module_file("models/catalogue-v1.json");
	const auto bytes = read_small_file(path);
	const std::string public_key = CORRIDORKEY_CATALOGUE_PUBLIC_KEY;
	Catalogue catalogue;
	if (public_key.empty()) {
		// No network/import path reaches installer admission. The exact bytes are pinned in the binary.
		catalogue = admit_installer_catalogue(cache, bytes, CORRIDORKEY_BUNDLED_CATALOGUE_SHA256);
	} else {
		const auto signature = strict_json(read_small_file(module_file("models/catalogue-signature.json")));
		fields(signature, {"signature"});
		catalogue = admit_catalogue(cache, bytes, string(signature.at("signature")), public_key);
	}
	std::vector<Manifest> manifests;
	for (const auto &pin : catalogue.entries)
		manifests.push_back(parse_manifest(read_small_file(path.parent_path() / pin.manifest.path), pin));
	return manifests;
}
ort::ProbeConfig installed_probe_config()
{
	ort::ProbeConfig config;
#ifdef _WIN32
	config.helper = module_file("tools/corridorkey_backend_probe.exe");
#else
	config.helper = utf8_path(CORRIDORKEY_PROBE_HELPER);
#endif
	config.scratch = obs_model_cache_path().parent_path() / "backend-tests";
	config.fixture_fp32 = module_file("tools/probe-fp32.onnx");
	config.fixture_fp16 = module_file("tools/probe-fp16.onnx");
	char *raw = obs_module_file("runtimes/probes.json");
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	if (raw) {
		const auto index = utf8_path(raw);
		auto j = strict_json(read_small_file(index));
		fields(j, {"schema_version", "bundles"});
		require(j.at("schema_version") == 1 && j.at("bundles").is_object(), "Invalid runtime index");
		for (const auto &[name, value] : j.at("bundles").items()) {
			const auto relative = string(value);
			relative_path(relative);
			config.bundles.emplace(ort::parse_backend(name), index.parent_path() / utf8_path(relative));
		}
	}
	// Optional bundles are installed by W19 from the executable-pinned runtime recipe.
	// A descriptor is never accepted from a model package or an environment variable.
	const auto recipe_bytes = read_small_file(module_file("runtimes/runtime-recipes-v1.json"));
	require(sha256(recipe_bytes) == CK_RUNTIME_CATALOGUE_SHA256, "Runtime catalogue differs from the executable",
		ErrorCode::hash_mismatch);
	const auto recipes = strict_json(recipe_bytes);
	const auto optional = obs_model_cache_path() / "runtime-cache" / "bundles";
	for (const auto &[id, recipe] : recipes.at("bundles").items()) {
		identifier(id);
		if (!recipe.value("enabled", false) || !recipe.contains("descriptors"))
			continue;
		for (const auto &entry : recipe.at("descriptors")) {
			const auto filename = string(entry.at("filename"));
			relative_path(filename);
			const auto descriptor = optional / id / filename;
			if (fs::is_regular_file(descriptor))
				config.bundles[ort::parse_backend(string(entry.at("backend")))] = descriptor;
		}
	}
	return config;
}
} // namespace corridorkey::models
