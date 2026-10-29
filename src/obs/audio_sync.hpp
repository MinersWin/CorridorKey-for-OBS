// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/settings.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>

namespace corridorkey {
// All operations, including release(), run on the same control thread.
class AudioOffsets {
public:
	virtual ~AudioOffsets() = default;
	virtual std::optional<std::int64_t> read(const std::string &uuid) = 0;
	virtual std::int64_t baseline(const std::string &, std::int64_t current) { return current; }
	virtual bool write(const std::string &uuid, std::int64_t expected, std::int64_t value) = 0;
};
struct AudioSyncOwners {
	std::map<std::string, const void *, std::less<>> microphones;
	std::set<std::string, std::less<>> manual_microphones;
};
enum class SyncState { off, waiting, active, shared, manual, missing, paused };
struct SyncSnapshot {
	SyncState state = SyncState::off;
	std::optional<std::int64_t> baseline_ns, applied_ns;
};
class AudioSyncController {
public:
	AudioSyncController(AudioSyncOwners &owners, AudioOffsets &offsets) : owners_(owners), offsets_(offsets) {}
	~AudioSyncController();
	AudioSyncController(const AudioSyncController &) = delete;
	AudioSyncController &operator=(const AudioSyncController &) = delete;
	SyncSnapshot step(const ContextSettings &settings, std::optional<double> delay_ms, bool healthy, TimePoint now,
			  bool manual_latched = false);
	void release();

private:
	AudioSyncOwners &owners_;
	AudioOffsets &offsets_;
	std::string uuid_, manual_uuid_;
	bool enabled_ = false;
	std::optional<std::int64_t> baseline_, owned_;
	std::optional<TimePoint> stable_since_, last_write_;
};
} // namespace corridorkey
