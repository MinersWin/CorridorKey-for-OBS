// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "download.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace corridorkey::models {
Catalogue admit_catalogue(Cache &, std::string_view bytes, std::string_view signature_hex,
			  std::string_view trusted_public_xy_hex);
// W05 writes only expected <graph>.weights files under cache.staging(manifest.key).
// The callback returns after complete materialization; Cache verifies each output.
using BindWeights = std::function<void(const Manifest &, Cache &, std::stop_token)>;
class CatalogueService final : public ModelCatalog {
	struct Job {
		Manifest manifest;
		std::stop_token stop;
		std::optional<fs::path> offline;
	};
	Cache &cache_;
	LicenceRegistry licences_;
	AcceptanceStore acceptance_;
	DownloadEngine downloads_;
	BindWeights binder_;
	std::map<std::pair<std::string, std::string>, Manifest> manifests_;
	mutable std::mutex mutex_;
	std::condition_variable_any changed_;
	std::map<std::pair<std::string, std::string>, ModelStatus> statuses_;
	std::set<std::pair<std::string, std::string>> pending_;
	std::deque<Job> jobs_;
	std::jthread worker_; // destroyed first, while services and mutexes remain alive
	void run(std::stop_token);
	void state(const ModelKey &, ModelState, Error = {}, Progress = {});
	Error enqueue(const ModelKey &, const std::vector<LicenceAcceptance> &, std::stop_token,
		      std::optional<fs::path>);

public:
	// Manifests must already be parsed against authenticated catalogue pins.
	// An empty callback selects the native binder and reports progress via snapshot().
	CatalogueService(Cache &, LicenceRegistry, std::vector<Manifest>, BindWeights = {},
			 std::shared_ptr<HttpClient> = system_http_client(), DownloadOptions = {});
	~CatalogueService() override;
	std::vector<ModelStatus> snapshot() const override;
	std::shared_ptr<const ReadyModel> acquire(const ModelKey &) override;
	Error request_install(const ModelKey &, const std::vector<LicenceAcceptance> &, std::stop_token) override;
	Error request_import(const ModelKey &, const std::vector<LicenceAcceptance> &, fs::path, std::stop_token);
};
} // namespace corridorkey::models
