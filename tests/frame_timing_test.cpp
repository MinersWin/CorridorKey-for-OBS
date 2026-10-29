// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "telemetry/frame_timing.hpp"
#include "obs/audio_sync.hpp"
#include "engine/fake_pipeline.hpp"
#include <doctest/doctest.h>

using namespace corridorkey;
using namespace std::chrono_literals;
TEST_CASE("displayed age counts repeated output and deduplicates multiple views")
{
	FrameTiming timing;
	FrameResult frame;
	frame.identity = {1, 1, 1, 1, TimePoint{} + 1s};
	frame.timings.inference = 40ms;
	frame.timings.input_transfer = 8ms;
	frame.timings.output_transfer = 2ms;
	frame.timings.submitted_at = frame.identity.captured_at + 10ms;
	for (unsigned i = 0; i < 5; ++i) {
		timing.record(frame, frame.identity.captured_at + 50ms + i * 10ms, i);
		timing.record(frame, frame.identity.captured_at + 50ms + i * 10ms, i);
	}
	const auto s = timing.snapshot(frame.identity.captured_at + 100ms);
	CHECK(s.presentations == 5);
	CHECK(s.repeated == 4);
	CHECK(s.age_median_ms == 70);
	CHECK(s.age_p95_ms == 90);
	CHECK(s.inference_median_ms == 40);
	CHECK(s.input_transfer_median_ms == 8);
	CHECK(s.output_transfer_median_ms == 2);
	CHECK(s.queue_median_ms == 2);
	CHECK_FALSE(timing.snapshot(frame.identity.captured_at + 6s).age_median_ms);
	frame.identity.generation = 2;
	timing.record(frame, frame.identity.captured_at + 100ms, 6);
	CHECK(timing.snapshot(frame.identity.captured_at + 100ms).presentations == 1);
	timing.reset();
	CHECK_FALSE(timing.snapshot(frame.identity.captured_at + 100ms).age_median_ms);
}
TEST_CASE("known fake delay propagates from input to readout and microphone offset")
{
	for (const auto delay : {0ms, 20ms, 80ms, 160ms}) {
		FramePool pool;
		FakeOptions options;
		options.delay = delay;
		FakePipeline pipeline(options, FakeExecution::manual);
		auto settings = std::make_shared<Settings>();
		PrepareRequest prepare;
		prepare.instance = 1;
		prepare.generation = pool.generation();
		prepare.settings = settings;
		REQUIRE_FALSE(pipeline.prepare(prepare));
		InputFrame input;
		input.lease = std::move(*pool.try_acquire());
		input.lease.transition(FrameState::ready);
		const auto origin = TimePoint{} + 1s;
		input.identity = {1, pool.generation(), 1, 1, origin};
		input.transform.source_size = {1, 1};
		input.analysis = {{1, 1},
				  PixelFormat::rgb_f32,
				  ColorSpace::srgb,
				  AlphaMode::opaque,
				  12,
				  std::make_shared<HostFloats>(std::vector<float>{1, 0, 0})};
		input.original = input.analysis;
		input.settings = settings;
		REQUIRE_FALSE(pipeline.try_submit(input, origin + 8ms));
		pipeline.advance(origin + 8ms + delay);
		auto result = pipeline.poll_result();
		REQUIRE(result);
		REQUIRE_FALSE(result->failure);
		CHECK(result->identity.captured_at == origin);
		FrameTiming timing;
		result->timings.input_transfer = 8ms;
		result->timings.output_transfer = 2ms;
		for (unsigned i = 0; i < 5; ++i)
			timing.record(*result, origin + delay + 10ms + i * 10ms, i);
		const auto snapshot = timing.snapshot(origin + 1s);
		CHECK(snapshot.age_median_ms == delay.count() + 30.0);
		CHECK(snapshot.age_p95_ms == delay.count() + 50.0);
		CHECK(snapshot.inference_median_ms == delay.count());
		struct Offset final : AudioOffsets {
			std::int64_t value = 17000000;
			std::optional<std::int64_t> read(const std::string &) override { return value; }
			bool write(const std::string &, std::int64_t expected, std::int64_t next) override
			{
				if (value != expected)
					return false;
				value = next;
				return true;
			}
		} offset;
		AudioSyncOwners owners;
		AudioSyncController sync(owners, offset);
		ContextSettings context;
		context.auto_sync = true;
		context.audio_source_id = "test";
		context.sync_trim_ms = 3;
		sync.step(context, snapshot.age_median_ms, true, origin);
		sync.step(context, snapshot.age_median_ms, true, origin + 2s);
		CHECK(offset.value == (17 + delay.count() + 30 + 3) * 1000000);
	}
}
