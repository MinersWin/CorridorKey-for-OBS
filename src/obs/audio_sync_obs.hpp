// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "audio_sync.hpp"
#include "contracts/status.hpp"
#include <obs.h>
#include <functional>
#include <memory>

namespace corridorkey {
class AudioSyncRuntime {
public:
	AudioSyncRuntime(obs_source_t *source, std::function<SettingsSnapshot()> settings,
			 std::function<StatusSnapshot()> status);
	~AudioSyncRuntime();
	void tick(bool immediately = false) noexcept;
	void stop() noexcept;
	SyncSnapshot snapshot() const;

private:
	struct Impl;
	std::shared_ptr<Impl> impl_;
};
} // namespace corridorkey
