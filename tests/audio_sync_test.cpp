// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "obs/audio_sync.hpp"
#include <doctest/doctest.h>

using namespace corridorkey;
using namespace std::chrono_literals;
namespace {
struct Offsets : AudioOffsets {
	std::map<std::string, std::int64_t> values{{"mic", 17000000}, {"other", -8000000}};
	unsigned writes = 0;
	std::optional<std::int64_t> read(const std::string &id) override
	{
		auto it = values.find(id);
		return it == values.end() ? std::nullopt : std::optional{it->second};
	}
	bool write(const std::string &id, std::int64_t expected, std::int64_t value) override
	{
		if (read(id) != expected)
			return false;
		values[id] = value;
		++writes;
		return true;
	}
};
struct Fixture {
	AudioSyncOwners owners;
	Offsets offsets;
	AudioSyncController controller{owners, offsets};
	ContextSettings settings;
	TimePoint at{};
	Fixture()
	{
		settings.auto_sync = true;
		settings.audio_source_id = "mic";
	}
	SyncSnapshot step(double delay, int seconds, bool healthy = true)
	{
		return controller.step(settings, delay, healthy, at + std::chrono::seconds(seconds));
	}
};
} // namespace
TEST_CASE("sync requires opt-in, explicit microphone and two seconds of valid samples")
{
	Fixture f;
	f.settings.auto_sync = false;
	CHECK(f.step(40, 0).state == SyncState::off);
	f.settings.auto_sync = true;
	f.settings.audio_source_id.clear();
	CHECK(f.step(40, 0).state == SyncState::waiting);
	f.settings.audio_source_id = "mic";
	CHECK(f.step(40, 0).state == SyncState::waiting);
	CHECK(f.step(40, 1).state == SyncState::waiting);
	CHECK(f.offsets.writes == 0);
	f.settings.sync_trim_ms = 3;
	CHECK(f.step(40, 2).applied_ns == 60000000);
	CHECK(f.step(40, 20).applied_ns == 60000000);
	CHECK(f.offsets.writes == 1);
}
TEST_CASE("sync hysteresis, rate bound, slew and cap use the baseline")
{
	Fixture f;
	f.step(40, 0);
	f.step(40, 2);
	CHECK(f.step(43, 4).applied_ns == 57000000);
	CHECK(f.step(80, 5).applied_ns == 87000000);
	CHECK(f.step(80, 6).applied_ns == 87000000);
	CHECK(f.step(80, 7).applied_ns == 97000000);
	CHECK(f.step(1000, 50).applied_ns == 267000000);
	f.settings.sync_trim_ms = -250;
	CHECK(f.step(10, 100).applied_ns == -223000000);
}
TEST_CASE("manual edits survive disable, error and destruction without reacquisition")
{
	Fixture f;
	f.step(40, 0);
	f.step(40, 2);
	f.offsets.values["mic"] = 99000000;
	CHECK(f.step(40, 3).state == SyncState::manual);
	CHECK(f.owners.microphones.empty());
	CHECK(f.step(40, 30).state == SyncState::manual);
	f.controller.release();
	CHECK(f.offsets.values["mic"] == 99000000);
	f.settings.auto_sync = false;
	f.step(40, 31);
	f.settings.auto_sync = true;
	f.step(40, 32);
	CHECK(f.step(40, 34).applied_ns == 139000000);
	f.step(40, 35, false);
	CHECK(f.offsets.values["mic"] == 99000000);
}
TEST_CASE("switch, failure, bypass and teardown restore only a matching owned value")
{
	Fixture f;
	f.step(40, 0);
	f.step(40, 2);
	f.settings.audio_source_id = "other";
	f.step(20, 3);
	CHECK(f.offsets.values["mic"] == 17000000);
	CHECK(f.step(20, 5).applied_ns == 12000000);
	f.step(20, 6, false);
	CHECK(f.offsets.values["other"] == -8000000);
	f.step(20, 7);
	f.step(20, 9);
	f.settings.auto_sync = false;
	f.step(20, 10);
	CHECK(f.offsets.values["other"] == -8000000);
	{
		AudioSyncController scoped(f.owners, f.offsets);
		f.settings.auto_sync = true;
		scoped.step(f.settings, 20, true, f.at);
		scoped.step(f.settings, 20, true, f.at + 2s);
	}
	CHECK(f.offsets.values["other"] == -8000000);
}
TEST_CASE("one controller per UUID, renamed and deleted microphones")
{
	Fixture f;
	AudioSyncController second(f.owners, f.offsets);
	f.step(40, 0);
	CHECK(second.step(f.settings, 80, true, f.at).state == SyncState::shared);
	f.step(40, 2);
	// Display names never enter the controller: a rename retains the same UUID.
	CHECK(f.step(40, 4).applied_ns == 57000000);
	f.offsets.values.erase("mic");
	CHECK(f.step(40, 5).state == SyncState::missing);
	CHECK(f.owners.microphones.empty());
	CHECK(second.step(f.settings, 80, true, f.at + 6s).state == SyncState::missing);
}
TEST_CASE("loss of valid video restarts the stable window and releases the reservation")
{
	Fixture f;
	f.step(40, 0);
	f.step(40, 1, false);
	CHECK(f.owners.microphones.empty());
	CHECK(f.step(40, 2).state == SyncState::waiting);
	CHECK(f.step(40, 3).state == SyncState::waiting);
	CHECK(f.step(40, 4).applied_ns == 57000000);
}
TEST_CASE("a manual edit also blocks an already waiting second controller")
{
	Fixture f;
	AudioSyncController second(f.owners, f.offsets);
	f.step(40, 0);
	CHECK(second.step(f.settings, 80, true, f.at).state == SyncState::shared);
	f.step(40, 2);
	f.offsets.values["mic"] = 99000000;
	CHECK(f.step(40, 3).state == SyncState::manual);
	CHECK(second.step(f.settings, 80, true, f.at + 4s).state == SyncState::manual);
	CHECK(f.offsets.values["mic"] == 99000000);
}
TEST_CASE("a manual edit during warmup releases the reservation without a write")
{
	Fixture f;
	f.step(40, 0);
	f.offsets.values["mic"] = 99000000;
	CHECK(f.step(40, 1).state == SyncState::manual);
	CHECK(f.offsets.writes == 0);
}
TEST_CASE("a persisted owned offset recovers its original baseline")
{
	struct Persisted : Offsets {
		std::int64_t baseline(const std::string &, std::int64_t) override { return 17000000; }
	} offsets;
	offsets.values["mic"] = 97000000;
	AudioSyncOwners owners;
	ContextSettings settings;
	settings.auto_sync = true;
	settings.audio_source_id = "mic";
	{
		AudioSyncController controller(owners, offsets);
		controller.step(settings, 80, true, TimePoint{});
		CHECK(controller.step(settings, 80, true, TimePoint{} + 2s).applied_ns == 97000000);
	}
	CHECK(offsets.values["mic"] == 17000000);
}
TEST_CASE("a saved manual latch survives reopening until an explicit toggle")
{
	Fixture f;
	f.offsets.values["mic"] = 99000000;
	CHECK(f.controller.step(f.settings, 40, true, f.at, true).state == SyncState::manual);
	CHECK(f.offsets.writes == 0);
	f.settings.auto_sync = false;
	f.controller.step(f.settings, 40, true, f.at + 1s);
	f.settings.auto_sync = true;
	f.controller.step(f.settings, 40, true, f.at + 2s);
	CHECK(f.controller.step(f.settings, 40, true, f.at + 4s).applied_ns == 139000000);
}
