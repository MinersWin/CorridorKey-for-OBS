// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "licenses.hpp"
#include <chrono>
#include <functional>
#include <span>
namespace corridorkey::models {
struct HttpResponse {
	unsigned status = 0;
	std::map<std::string, std::string> headers; // lowercase names
	std::string header(std::string_view name) const;
};
struct HttpRequest {
	std::string url;
	std::uint64_t offset = 0;
	std::string validator;
};
using HeaderSink = std::function<bool(const HttpResponse &)>;
using ByteSink = std::function<void(std::span<const char>)>;
class HttpClient {
public:
	virtual ~HttpClient() = default;
	virtual void get(const HttpRequest &, const HeaderSink &, const ByteSink &, std::stop_token) = 0;
};
// Test policy admits only literal 127.0.0.1 HTTP; manifests still require HTTPS.
std::shared_ptr<HttpClient> system_http_client(bool loopback_test_policy = false);
struct DownloadOptions {
	unsigned attempts = 5;
	std::function<bool(std::chrono::milliseconds, std::stop_token)> wait;
	std::function<void(std::uint64_t)> check_space; // default queries cache's actual volume
};
class DownloadEngine {
	Cache &cache_;
	std::shared_ptr<HttpClient> http_;
	DownloadOptions options_;

public:
	DownloadEngine(Cache &, std::shared_ptr<HttpClient> = system_http_client(), DownloadOptions = {});
	// Caller must supply a valid acceptance store for this exact package.
	void fetch(const Manifest &, const Artifact &, const LicenceRegistry &, const AcceptanceStore &,
		   std::stop_token = {}, std::function<void(Progress)> = {});
	// Folder/ZIP convention: blobs/<sha256>. No archive paths become cache paths.
	void import_offline(const Manifest &, const fs::path &folder_or_zip, const LicenceRegistry &,
			    const AcceptanceStore &, std::stop_token = {});
};
} // namespace corridorkey::models
