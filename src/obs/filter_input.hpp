// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/status.hpp"
#include "contracts/settings.hpp"
#include <obs.h>
#include <memory>

namespace corridorkey {
class FilterInput {
public:
	explicit FilterInput(obs_source_t *source);
	~FilterInput(); // Teardown queue only: joins the transfer/inference worker.
	void update(obs_data_t *data, SettingsSnapshot settings);
	void reset() noexcept;
	void tick() noexcept;
	bool render() noexcept;
	StatusSnapshot status() const;
	static void add_properties(obs_properties_t *properties);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey
