// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "cache.hpp"
namespace corridorkey::models {
struct LicenceRecord {
	std::string id, text, digest, source_url, attribution, scope;
};
class LicenceRegistry {
	std::map<std::string, LicenceRecord> records_;

public:
	static LicenceRegistry parse(std::string_view);
	const LicenceRecord &at(const LicenceAcceptance &) const;
};
class AcceptanceStore {
	Cache &cache_;
	std::string context_;

public:
	explicit AcceptanceStore(Cache &cache, std::string local_context = "local-user");
	// Invoke only after the UI has shown these exact texts and the user accepted.
	void accept(const Manifest &, const LicenceRegistry &, const std::vector<LicenceAcceptance> &);
	bool contains(const Manifest &, const LicenceRegistry &) const;
};
std::string licence_notices(const Manifest &, const LicenceRegistry &);
} // namespace corridorkey::models
