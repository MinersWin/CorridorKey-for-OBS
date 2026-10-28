// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "status.hpp"
#include <filesystem>
#include <stop_token>
#include <vector>

namespace corridorkey {
struct ModelKey {
	std::string id, revision;
	bool operator==(const ModelKey &) const = default;
};
struct LicenceAcceptance {
	std::string licence_id, text_sha256;
};
struct GraphArtifact {
	std::string role, graph_sha256, weights_sha256, recipe_version;
	std::filesystem::path graph_path, weights_path;
};
// W04 implements a lease whose lifetime prevents deletion of this revision.
class ModelReadLease {
public:
	virtual ~ModelReadLease() = default;
};
struct ReadyModel {
	ModelKey key;
	std::string manifest_sha256;
	std::vector<GraphArtifact> graphs;
	std::shared_ptr<const ModelReadLease> lease;
};
struct ModelStatus {
	ModelKey key;
	ModelState state = ModelState::missing;
	Progress progress;
	Error failure;
};
class ModelCatalog {
public:
	virtual ~ModelCatalog() = default;
	// Control/loader context only. Ready revisions remain immutable while leased.
	virtual std::vector<ModelStatus> snapshot() const = 0;
	virtual std::shared_ptr<const ReadyModel> acquire(const ModelKey &) = 0;
	// Enqueue and copy the request before returning; W04 validates acceptance.
	virtual Error request_install(const ModelKey &, const std::vector<LicenceAcceptance> &, std::stop_token) = 0;
};
} // namespace corridorkey
