// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "models/download.hpp"

namespace corridorkey::packaging {
struct RuntimePackage {
	models::Manifest download;
	models::LicenceRegistry licences;
	models::Json recipe;
};
// Only installer-owned metadata with the executable's compiled digest is trusted.
RuntimePackage runtime_package(std::string_view bytes, std::string_view trusted_sha256, std::string_view id);
void extract_runtime_member(const models::fs::path &archive, std::string_view member,
			    const models::fs::path &destination, std::uint64_t expected_bytes, std::stop_token = {});
models::fs::path install_runtime(const RuntimePackage &, models::Cache &, const models::AcceptanceStore &,
				 std::stop_token = {}, std::function<void(Progress)> = {});
} // namespace corridorkey::packaging
