// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "contracts/settings.hpp"
#include "contracts/status.hpp"
#include <obs.h>

namespace corridorkey {
// Borrowed only during an OBS callback. UI timers must use their own weak owner.
class FilterControl {
public:
	virtual ~FilterControl() = default;
	virtual SettingsSnapshot settings_snapshot() const = 0;
	virtual StatusSnapshot status_snapshot() const = 0;
	virtual obs_source_t *property_source() const { return nullptr; }
	virtual void request_reset() noexcept = 0;
};
} // namespace corridorkey
