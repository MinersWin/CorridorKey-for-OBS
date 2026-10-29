// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "licenses.hpp"
#include <chrono>
namespace corridorkey::models {
LicenceRegistry LicenceRegistry::parse(std::string_view raw)
{
	auto j = strict_json(raw);
	fields(j, {"schema_version", "licenses"}, {"$comment"});
	require(uint64(j.at("schema_version")) == 1 && j.at("licenses").is_array() && j.at("licenses").size() <= 64,
		"Invalid licence registry");
	LicenceRegistry registry;
	for (auto &l : j.at("licenses")) {
		fields(l, {"id", "text", "text_sha256", "source_url", "attribution", "scope"});
		LicenceRecord r{string(l.at("id")),          string(l.at("text"), 256 * 1024),
				string(l.at("text_sha256")), string(l.at("source_url")),
				string(l.at("attribution")), string(l.at("scope"))};
		identifier(r.id);
		https_url(r.source_url);
		require(hex_string(r.digest, 64) && sha256(r.text) == r.digest, "Licence text digest mismatch",
			ErrorCode::hash_mismatch);
		require(registry.records_.emplace(r.id, r).second, "Duplicate licence");
	}
	return registry;
}
const LicenceRecord &LicenceRegistry::at(const LicenceAcceptance &a) const
{
	auto it = records_.find(a.licence_id);
	require(it != records_.end() && it->second.digest == a.text_sha256, "Unknown or changed licence",
		ErrorCode::licence_required);
	return it->second;
}
AcceptanceStore::AcceptanceStore(Cache &c, std::string context) : cache_(c), context_(std::move(context))
{
	require(!context_.empty() && context_.size() <= 256, "Invalid local user context");
}
void AcceptanceStore::accept(const Manifest &m, const LicenceRegistry &registry,
			     const std::vector<LicenceAcceptance> &accepted)
{
	require(accepted.size() == m.licences.size(), "Acceptance does not cover all licences",
		ErrorCode::licence_required);
	std::set<std::pair<std::string, std::string>> supplied;
	for (auto &a : accepted)
		require(supplied.emplace(a.licence_id, a.text_sha256).second, "Duplicate acceptance",
			ErrorCode::licence_required);
	Json receipts = Json::array();
	for (auto &a : m.licences) {
		(void)registry.at(a);
		require(supplied.contains({a.licence_id, a.text_sha256}), "Acceptance text hash mismatch",
			ErrorCode::licence_required);
		receipts.push_back({{"id", a.licence_id}, {"text_sha256", a.text_sha256}});
	}
	FileLock lock(cache_.path("locks/acceptance.lock"), false, true);
	auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
			   .count();
	atomic_write(cache_.path("receipts/" + m.digest + ".json"), Json{{"schema_version", 1},
									 {"id", m.key.id},
									 {"revision", m.key.revision},
									 {"manifest_sha256", m.digest},
									 {"accepted_unix_seconds", now},
									 {"local_context", context_},
									 {"licenses", receipts}}
									    .dump());
}
bool AcceptanceStore::contains(const Manifest &m, const LicenceRegistry &registry) const
{
	for (auto &a : m.licences)
		(void)registry.at(a);
	auto p = cache_.path("receipts/" + m.digest + ".json");
	if (!fs::exists(p))
		return false;
	try {
		auto j = strict_json(read_small_file(p));
		fields(j, {"schema_version", "id", "revision", "manifest_sha256", "accepted_unix_seconds",
			   "local_context", "licenses"});
		require(uint64(j.at("schema_version")) == 1 && string(j.at("id")) == m.key.id &&
				string(j.at("revision")) == m.key.revision &&
				string(j.at("manifest_sha256")) == m.digest &&
				uint64(j.at("accepted_unix_seconds")) > 0,
			"Invalid acceptance receipt");
		(void)string(j.at("local_context"));
		require(j.at("licenses").is_array() && j.at("licenses").size() == m.licences.size(),
			"Incomplete receipt");
		std::set<std::pair<std::string, std::string>> seen;
		for (auto &a : j.at("licenses")) {
			fields(a, {"id", "text_sha256"});
			require(seen.emplace(string(a.at("id")), string(a.at("text_sha256"))).second,
				"Duplicate receipt");
		}
		for (auto &a : m.licences)
			if (!seen.contains({a.licence_id, a.text_sha256}))
				return false;
		return true;
	} catch (const Failure &) {
		return false;
	}
}
std::string licence_notices(const Manifest &m, const LicenceRegistry &registry)
{
	std::string out = m.display_name + "\nPublisher: " + m.publisher + "\nRevision: " + m.key.revision + "\n";
	for (auto &a : m.licences) {
		auto &r = registry.at(a);
		out += "\n" + r.id + "\n" + r.attribution + "\nScope: " + r.scope + "\n" + r.source_url + "\n\n" +
		       r.text + "\n";
	}
	return out;
}
} // namespace corridorkey::models
