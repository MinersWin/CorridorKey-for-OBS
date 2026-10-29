// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "audio_sync_obs.hpp"
#include "telemetry/obs_clock.hpp"
#include <atomic>
#include <mutex>

namespace corridorkey {
namespace {
AudioSyncOwners owners;
class ObsOffsets final : public AudioOffsets {
	std::int64_t baseline_ = 0;

public:
	std::int64_t baseline(const std::string &uuid, std::int64_t current) override
	{
		baseline_ = current;
		auto *source = obs_get_source_by_uuid(uuid.c_str());
		if (source) {
			auto *data = obs_source_get_private_settings(source);
			if (obs_data_get_bool(data, "corridorkey_sync_owned") &&
			    obs_data_get_int(data, "corridorkey_sync_last_ns") == current)
				baseline_ = obs_data_get_int(data, "corridorkey_sync_baseline_ns");
			obs_data_release(data);
		}
		obs_source_release(source);
		return baseline_;
	}
	std::optional<std::int64_t> read(const std::string &uuid) override
	{
		auto *source = obs_get_source_by_uuid(uuid.c_str());
		std::optional<std::int64_t> result;
		if (source && !obs_source_removed(source) && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
			result = obs_source_get_sync_offset(source);
		obs_source_release(source);
		return result;
	}
	bool write(const std::string &uuid, std::int64_t expected, std::int64_t value) override
	{
		auto *source = obs_get_source_by_uuid(uuid.c_str());
		const bool matches = source && !obs_source_removed(source) &&
				     obs_source_get_sync_offset(source) == expected;
		if (matches) {
			obs_source_set_sync_offset(source, value);
			auto *data = obs_source_get_private_settings(source);
			obs_data_set_bool(data, "corridorkey_sync_owned", value != baseline_);
			obs_data_set_int(data, "corridorkey_sync_baseline_ns", baseline_);
			obs_data_set_int(data, "corridorkey_sync_last_ns", value);
			obs_data_release(data);
		}
		obs_source_release(source);
		return matches;
	}
};
} // namespace
struct AudioSyncRuntime::Impl {
	obs_weak_source_t *source;
	std::function<SettingsSnapshot()> settings;
	std::function<StatusSnapshot()> status;
	ObsOffsets offsets;
	AudioSyncController controller{owners, offsets};
	std::atomic<bool> stopped{false}, pending{false};
	std::atomic<std::uint64_t> next{0};
	mutable std::mutex mutex;
	SyncSnapshot current;
	Impl(obs_source_t *owner, std::function<SettingsSnapshot()> read_settings,
	     std::function<StatusSnapshot()> read_status)
		: source(obs_source_get_weak_source(owner)),
		  settings(std::move(read_settings)),
		  status(std::move(read_status))
	{
	}
	~Impl() { obs_weak_source_release(source); }
	void refresh()
	{
		if (stopped) {
			controller.release();
			return;
		}
		auto *owner = obs_weak_source_get_source(source);
		if (!owner) {
			controller.release();
			return;
		}
		try {
			const auto s = settings();
			const auto state = status();
			SyncSnapshot value;
			if (s && state) {
				const bool healthy = obs_filter_get_parent(owner) && obs_source_enabled(owner) &&
						     !obs_source_removed(owner) && !s->context.bypass &&
						     !state->failure && state->lifecycle == Lifecycle::ready;
				auto *private_settings = obs_source_get_private_settings(owner);
				const bool manual =
					s->context.auto_sync &&
					s->context.audio_source_id ==
						obs_data_get_string(private_settings, "corridorkey_sync_manual_uuid");
				value = controller.step(s->context, state->age_median_ms, healthy, obs_now(), manual);
				obs_data_set_string(
					private_settings, "corridorkey_sync_manual_uuid",
					value.state == SyncState::manual ? s->context.audio_source_id.c_str() : "");
				obs_data_release(private_settings);
			} else
				controller.release();
			std::lock_guard lock(mutex);
			current = value;
		} catch (...) {
			controller.release();
		}
		obs_source_release(owner);
	}
};
AudioSyncRuntime::AudioSyncRuntime(obs_source_t *source, std::function<SettingsSnapshot()> settings,
				   std::function<StatusSnapshot()> status)
	: impl_(new Impl(source, std::move(settings), std::move(status)), [](Impl *value) {
		  // The last reference can leave the destroy queue after the UI release task.
		  // Keep the controller and its registry on the UI thread through destruction.
		  obs_queue_task(OBS_TASK_UI, [](void *data) { delete static_cast<Impl *>(data); }, value, false);
	  })
{
}
AudioSyncRuntime::~AudioSyncRuntime()
{
	stop();
}
void AudioSyncRuntime::tick(bool immediately) noexcept
{
	const auto ns = os_gettime_ns();
	if ((!immediately && ns < impl_->next.load()) || impl_->pending.exchange(true))
		return;
	impl_->next = ns + 500000000;
	try {
		auto *task = new std::shared_ptr<Impl>(impl_);
		obs_queue_task(
			OBS_TASK_UI,
			[](void *opaque) {
				std::unique_ptr<std::shared_ptr<Impl>> task(
					static_cast<std::shared_ptr<Impl> *>(opaque));
				(*task)->refresh();
				(*task)->pending = false;
			},
			task, false);
	} catch (...) {
		impl_->pending = false;
	}
}
void AudioSyncRuntime::stop() noexcept
{
	impl_->stopped = true;
	tick(true);
}
SyncSnapshot AudioSyncRuntime::snapshot() const
{
	std::lock_guard lock(impl_->mutex);
	return impl_->current;
}
} // namespace corridorkey
