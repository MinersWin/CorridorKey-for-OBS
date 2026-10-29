// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "audio_sync.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace corridorkey {
using namespace std::chrono_literals;
AudioSyncController::~AudioSyncController()
{
	release();
}
void AudioSyncController::release()
{
	if (owned_ && baseline_ && *owned_ != *baseline_)
		offsets_.write(uuid_, *owned_, *baseline_);
	if (auto it = owners_.microphones.find(uuid_); it != owners_.microphones.end() && it->second == this)
		owners_.microphones.erase(it);
	baseline_.reset();
	owned_.reset();
	stable_since_.reset();
	last_write_.reset();
}
SyncSnapshot AudioSyncController::step(const ContextSettings &s, std::optional<double> delay_ms, bool healthy,
				       TimePoint now, bool manual_latched)
{
	if (s.auto_sync && (!enabled_ || s.audio_source_id != uuid_))
		owners_.manual_microphones.erase(s.audio_source_id);
	if (s.audio_source_id != uuid_ || !s.auto_sync) {
		release();
		manual_uuid_.clear();
		uuid_ = s.audio_source_id;
	}
	enabled_ = s.auto_sync;
	if (!enabled_ || uuid_.empty())
		return {enabled_ ? SyncState::waiting : SyncState::off, {}, {}};
	if (manual_latched) {
		manual_uuid_ = uuid_;
		owners_.manual_microphones.insert(uuid_);
	}
	const auto current = offsets_.read(uuid_);
	if (!current) {
		release();
		return {SyncState::missing, {}, {}};
	}
	if (baseline_ && *current != owned_.value_or(*baseline_)) {
		manual_uuid_ = uuid_;
		owners_.manual_microphones.insert(uuid_);
		// A manual edit is authoritative: do not restore or adopt it as a baseline.
		owned_.reset();
		release();
	}
	if (manual_uuid_ == uuid_ || owners_.manual_microphones.contains(uuid_))
		return {SyncState::manual, {}, current};
	if (!healthy || !delay_ms || !std::isfinite(*delay_ms) || *delay_ms < 0) {
		release();
		return {SyncState::paused, {}, offsets_.read(uuid_)};
	}
	if (auto it = owners_.microphones.find(uuid_); it != owners_.microphones.end() && it->second != this)
		return {SyncState::shared, {}, current};
	owners_.microphones[uuid_] = this;
	if (!baseline_) {
		baseline_ = offsets_.baseline(uuid_, *current);
		owned_ = current;
	}
	if (!stable_since_)
		stable_since_ = now;
	if (now - *stable_since_ < 2s)
		return {SyncState::waiting, baseline_, current};
	// Bound the complete added correction, including trim. OBS stores signed ns.
	const auto added = static_cast<std::int64_t>(
		std::llround(std::clamp(*delay_ms + s.sync_trim_ms, -static_cast<double>(s.sync_limit_ms),
					static_cast<double>(s.sync_limit_ms)) *
			     1000000.0));
	const auto base = std::clamp<std::int64_t>(*baseline_, std::numeric_limits<std::int64_t>::min() + 1000000000LL,
						   std::numeric_limits<std::int64_t>::max() - 1000000000LL);
	auto desired = base + added;
	if (last_write_) {
		if (now - *last_write_ < 2s)
			return {SyncState::active, baseline_, owned_};
		const auto slew = static_cast<std::int64_t>(
			std::min(1000.0, std::chrono::duration<double>(now - *last_write_).count()) * 10000000.0);
		const auto lower = *owned_ < std::numeric_limits<std::int64_t>::min() + slew
					   ? std::numeric_limits<std::int64_t>::min()
					   : *owned_ - slew;
		const auto upper = *owned_ > std::numeric_limits<std::int64_t>::max() - slew
					   ? std::numeric_limits<std::int64_t>::max()
					   : *owned_ + slew;
		desired = std::clamp(desired, lower, upper);
	}
	if (std::abs(static_cast<long double>(desired) - *current) >= 5000000.0L) {
		if (!offsets_.write(uuid_, *current, desired)) {
			release();
			manual_uuid_ = uuid_;
			return {SyncState::manual, {}, offsets_.read(uuid_)};
		}
		owned_ = desired;
		last_write_ = now;
	} else if (!owned_) {
		// Reserve an unchanged value so a later external edit still releases ownership.
		owned_ = current;
	}
	return {SyncState::active, baseline_, owned_};
}
} // namespace corridorkey
