// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/models.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace corridorkey::models {
using Json = nlohmann::json;
namespace fs = std::filesystem;
class Failure : public std::runtime_error {
public:
	ErrorCode code;
	Failure(ErrorCode c, const std::string &message) : std::runtime_error(message), code(c) {}
};
void require(bool condition, std::string_view message, ErrorCode code = ErrorCode::invalid_input);
Json strict_json(std::string_view bytes, std::size_t limit = 4 * 1024 * 1024);
void fields(const Json &, std::initializer_list<std::string_view> required,
	    std::initializer_list<std::string_view> optional = {});
std::uint64_t uint64(const Json &);
std::uint64_t checked_add(std::uint64_t, std::uint64_t);
std::string string(const Json &, std::size_t max = 4096);
bool hex_string(std::string_view, std::size_t digits);
void identifier(std::string_view);
void relative_path(std::string_view);
void https_url(std::string_view);
std::string sha256(std::string_view);
std::string file_sha256(const fs::path &, std::stop_token = {});
bool verify_signature(std::string_view bytes, std::string_view public_xy_hex, std::string_view signature_hex);

struct Artifact {
	std::string id, path, format, sha256, url, revision;
	std::uint64_t bytes = 0;
	std::vector<std::string> licences;
};
struct Graph {
	std::string id, role, graph, metadata, external_data, weights_sha256;
	std::uint64_t weights_bytes = 0;
	std::vector<std::string> sources;
	Json inputs, outputs;
};
struct Manifest {
	ModelKey key;
	std::string digest, raw, display_name, publisher, pipeline, description;
	Json profiles, compatibility;
	std::vector<LicenceAcceptance> licences;
	std::map<std::string, Artifact> artifacts;
	std::vector<Graph> graphs;
	std::uint64_t bound_bytes = 0, workspace_bytes = 0, engine_bytes = 0, margin_bytes = 0, staged_graph_bytes = 0;
	std::uint64_t peak_additional_bytes() const;
};
// expected_sha256 comes from an authenticated catalogue, never from the same payload.
Manifest parse_manifest(std::string_view bytes, std::string_view expected_sha256);
std::map<std::string, Artifact> parse_source_inventory(std::string_view bytes);
// Validates the actual exporter JSON; execution and materialization belong to W05.
void validate_export_metadata(std::string_view bytes, const Graph &, const Manifest &);

struct CatalogueEntry {
	ModelKey key;
	Artifact manifest;
};
Manifest parse_manifest(std::string_view bytes, const CatalogueEntry &pin);
struct Catalogue {
	std::uint64_t sequence = 0;
	std::vector<CatalogueEntry> entries;
};
// P-256/SHA-256, raw 64-byte r||s; trusted public keys are supplied by the host.
Catalogue parse_catalogue(std::string_view bytes, std::string_view signature_hex,
			  std::string_view trusted_public_xy_hex, std::uint64_t minimum_sequence);
// Only the host's compiled digest of its installer payload may supply this pin.
Catalogue parse_installer_catalogue(std::string_view bytes, std::string_view compiled_sha256,
				    std::uint64_t minimum_sequence);
} // namespace corridorkey::models
