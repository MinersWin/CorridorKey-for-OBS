// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/catalog.hpp"
#include "util/utf8_path.hpp"
#include <iostream>
#include <thread>

using namespace corridorkey;
namespace m = corridorkey::models;
int main(int argc, char **argv)
{
	if (argc != 6) {
		std::cerr << "Usage: ck1_catalogue DATA EXPORTS SOURCE_MAP.json CACHE REPORT.json\n";
		return 2;
	}
	m::Json report;
	try {
		const auto data = utf8_path(argv[1]), exports = utf8_path(argv[2]);
		const auto sources = m::strict_json(m::read_small_file(utf8_path(argv[3])));
		m::Cache cache(utf8_path(argv[4]));
		auto licences = m::LicenceRegistry::parse(m::read_small_file(data / "licenses-v1.json"));
		const auto raw = m::read_small_file(data / "catalogue-v1.json");
		const auto catalogue = m::admit_installer_catalogue(cache, raw, m::sha256(raw));
		std::vector<m::Manifest> manifests;
		for (const auto &pin : catalogue.entries)
			if (pin.key.id == "ck1-green" || pin.key.id == "ck1-blue" || pin.key.id == "modnet")
				manifests.push_back(
					m::parse_manifest(m::read_small_file(data / pin.manifest.path), pin));
		const auto offline = cache.path("validation-import");
		m::fs::create_directories(offline / "blobs");
		for (const auto &manifest : manifests)
			for (const auto &[id, artifact] : manifest.artifacts) {
				const auto path = artifact.format == "safetensors"
							  ? utf8_path(sources.at(id).get<std::string>())
							  : exports / m::fs::path(artifact.path).filename();
				m::require(m::fs::file_size(path) == artifact.bytes &&
						   m::file_sha256(path) == artifact.sha256,
					   "Local source differs from catalogue", ErrorCode::hash_mismatch);
				const auto destination = offline / "blobs" / artifact.sha256;
				if (!m::fs::exists(destination))
					m::fs::copy_file(path, destination);
			}
		m::CatalogueService service(cache, licences, manifests);
		for (const auto &manifest : manifests) {
			m::require(!m::AcceptanceStore(cache).contains(manifest, licences),
				   "Use a fresh validation cache");
			auto rejected = service.request_import(manifest.key, {}, offline, {});
			m::require(rejected.code == ErrorCode::licence_required, "Missing acceptance was not rejected");
			m::require(!service.request_import(manifest.key, manifest.licences, offline, {}),
				   "Import failed");
			const auto deadline = Clock::now() + std::chrono::minutes(10);
			bool ready = false;
			while (!ready && Clock::now() < deadline) {
				for (const auto &status : service.snapshot())
					if (status.key == manifest.key) {
						if (status.state == ModelState::failed)
							throw m::Failure(status.failure.code,
									 "Catalogue installation failed");
						ready = status.state == ModelState::ready;
					}
				if (!ready)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			m::require(ready, "Catalogue import timeout");
			auto model = service.acquire(manifest.key);
			m::require(bool(model), "Verified package acquisition failed");
			report[manifest.key.id] = {{"manifest_sha256", model->manifest_sha256},
						   {"bound_graphs", model->graphs.size()},
						   {"missing_acceptance_rejected", true},
						   {"accepted_and_acquired", true}};
		}
	} catch (const std::exception &e) {
		report["error"] = e.what();
	}
	m::atomic_write(utf8_path(argv[5]), report.dump(2) + "\n");
	std::cout << report.dump(2) << '\n';
	return report.contains("error") ? 1 : 0;
}
