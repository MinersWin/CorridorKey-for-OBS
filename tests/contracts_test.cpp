// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "engine/fake_pipeline.hpp"
#include <atomic>
#include <future>
#include <limits>

using namespace corridorkey;
using namespace std::chrono_literals;

namespace {
const TimePoint origin{1s};
struct Fixture {
	FramePool pool;
	FakePipeline pipeline;
	SettingsSnapshot settings = std::make_shared<const Settings>();
	Image image = synthetic_rgb({17, 9});
	explicit Fixture(FakeOptions options = {}, FakeExecution mode = FakeExecution::manual) : pipeline(options, mode)
	{
		PrepareRequest request;
		request.instance = 42;
		request.generation = pool.generation();
		request.settings = settings;
		REQUIRE_FALSE(pipeline.prepare(request));
	}
	InputFrame frame(Sequence sequence, TimePoint captured = origin)
	{
		auto lease = pool.try_acquire();
		REQUIRE(lease.has_value());
		InputFrame input;
		input.lease = std::move(*lease);
		REQUIRE(input.lease.transition(FrameState::ready));
		input.identity = {42, pool.generation(), sequence, sequence * 10, captured};
		input.transform.source_size = image.size;
		input.original = input.analysis = image;
		input.settings = settings;
		return input;
	}
};
struct Fence final : Completion {
	std::atomic<bool> signalled{false};
	bool ready() const noexcept override { return signalled.load(); }
};
} // namespace

TEST_CASE("pool bounds, exclusive moves and reuse IDs prevent early reuse")
{
	FramePool pool;
	auto a = pool.try_acquire();
	auto b = pool.try_acquire();
	auto c = pool.try_acquire();
	REQUIRE(a);
	REQUIRE(b);
	REQUIRE(c);
	CHECK(pool.in_use() == 3);
	CHECK_FALSE(pool.try_acquire());
	CHECK_FALSE(a->transition(FrameState::presenting));
	const auto old_id = a->id();
	FrameLease moved = std::move(*a);
	CHECK_FALSE(static_cast<bool>(*a));
	CHECK(moved.transition(FrameState::ready));
	CHECK(moved.transition(FrameState::processing));
	CHECK(moved.transition(FrameState::complete));
	CHECK(moved.transition(FrameState::presenting));
	CHECK(pool.advance_generation() == 2);
	CHECK_FALSE(moved.current());
	CHECK_FALSE(pool.try_acquire());
	CHECK(moved.transition(FrameState::retired));
	moved.release();
	auto replacement = pool.try_acquire();
	REQUIRE(replacement);
	CHECK(replacement->id().index == old_id.index);
	CHECK(replacement->id().reuse > old_id.reuse);
	CHECK(replacement->generation() == 2);
}

TEST_CASE("leases survive their pool owner")
{
	FrameLease retained;
	{
		FramePool pool;
		retained = std::move(*pool.try_acquire());
	}
	CHECK(retained.current());
	retained.release();
	CHECK_FALSE(static_cast<bool>(retained));
}

TEST_CASE("deterministic odd-sized RGB and alpha retain exact origin and transform")
{
	Fixture fixture({20ms, 0ms});
	auto input = fixture.frame(1);
	input.transform.user_crop = {0.1f, 0.2f, 0.8f, 0.9f};
	input.transform.sampled_roi = input.transform.user_crop;
	const auto identity = input.identity;
	const auto transform = input.transform;
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin + 2ms));
	CHECK_FALSE(static_cast<bool>(input.lease));
	fixture.pipeline.advance(origin + 21ms);
	CHECK_FALSE(fixture.pipeline.poll_result());
	fixture.pipeline.advance(origin + 22ms);
	auto output = fixture.pipeline.poll_result();
	REQUIRE(output);
	CHECK(output->identity == identity);
	CHECK(output->transform == transform);
	CHECK(output->input.original.storage == fixture.image.storage);
	CHECK(output->input.settings == fixture.settings);
	CHECK(output->timings.inference == 20ms);
	CHECK_FALSE(validate_result(*output));
	const auto rgb = fixture.image.storage->host_floats();
	const auto foreground = output->foreground.storage->host_floats();
	const auto alpha = output->alpha.storage->host_floats();
	REQUIRE(foreground.size() == rgb.size());
	for (std::size_t i = 0; i < alpha.size(); ++i) {
		CHECK(alpha[i] == (rgb[i * 3] + rgb[i * 3 + 2]) * 0.5f);
		for (std::size_t c = 0; c < 3; ++c)
			CHECK(foreground[i * 3 + c] == rgb[i * 3 + c]);
	}
	CHECK(presentable(*output, 42, 1, 0, origin + 250ms, 250ms));
	CHECK_FALSE(presentable(*output, 42, 1, 0, origin + 251ms, 250ms));
	CHECK_FALSE(presentable(*output, 43, 1, 0, origin + 22ms, 250ms));
	CHECK_FALSE(presentable(*output, 42, 1, 0, origin - 1ms, 250ms));
	CHECK(fixture.pool.in_use() == 1);
	output.reset();
	CHECK(fixture.pool.in_use() == 0);
}

TEST_CASE("out-of-order completion cannot roll presentation backward")
{
	Fixture fixture({10ms, 50ms});
	auto first = fixture.frame(1);
	auto second = fixture.frame(2, origin + 1ms);
	REQUIRE_FALSE(fixture.pipeline.try_submit(first, origin));
	REQUIRE_FALSE(fixture.pipeline.try_submit(second, origin + 1ms));
	fixture.pipeline.advance(origin + 11ms);
	auto newer = fixture.pipeline.poll_result();
	REQUIRE(newer);
	CHECK(newer->identity.sequence == 2);
	CHECK(newer->identity.captured_at == origin + 1ms);
	fixture.pipeline.advance(origin + 60ms);
	auto older = fixture.pipeline.poll_result();
	REQUIRE(older);
	CHECK(older->identity.sequence == 1);
	CHECK_FALSE(presentable(*older, 42, 1, 2, origin + 60ms, 250ms));
	CHECK(presentable(*newer, 42, 1, 2, origin + 60ms, 250ms));
}

TEST_CASE("generation change rejects retained results and releases queued work")
{
	Fixture fixture;
	auto input = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
	fixture.pipeline.advance(origin);
	auto retained = fixture.pipeline.poll_result();
	REQUIRE(retained);
	auto queued = fixture.frame(2);
	REQUIRE_FALSE(fixture.pipeline.try_submit(queued, origin));
	const auto generation = fixture.pool.advance_generation();
	REQUIRE_FALSE(fixture.pipeline.reset(generation, ResetReason::dimensions));
	CHECK_FALSE(presentable(*retained, 42, generation, 0, origin, 250ms));
	CHECK(validate_result(*retained).code == ErrorCode::stale_generation);
	CHECK(fixture.pool.in_use() == 1);
	CHECK(fixture.pipeline.diagnostics().outstanding == 0);
	CHECK(fixture.pipeline.reset(generation, ResetReason::explicit_reset).code == ErrorCode::stale_generation);
	auto fresh = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(fresh, origin));
	fixture.pipeline.advance(origin);
	REQUIRE(fixture.pipeline.poll_result());
}

TEST_CASE("completed queues stay bounded even when callers supply other pools")
{
	Fixture fixture;
	for (Sequence sequence = 1; sequence <= 3; ++sequence) {
		auto input = fixture.frame(sequence);
		REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
		fixture.pipeline.advance(origin);
	}
	CHECK(fixture.pipeline.diagnostics().outstanding == 3);
	CHECK_FALSE(fixture.pool.try_acquire());
	Fixture extra;
	auto fourth = extra.frame(4);
	CHECK(fixture.pipeline.try_submit(fourth, origin).code == ErrorCode::busy);
	CHECK(static_cast<bool>(fourth.lease));
	fixture.pipeline.shutdown();
	CHECK(fixture.pool.in_use() == 0);
	CHECK(fixture.pipeline.diagnostics().outstanding == 0);
}

TEST_CASE("faults are typed, invalid numbers are discarded before presentation")
{
	for (const auto fault : {FakeFault::out_of_memory, FakeFault::invalid_output, FakeFault::backend_unavailable}) {
		Fixture fixture({0ms, 0ms, fault, 2});
		auto good = fixture.frame(1);
		REQUIRE_FALSE(fixture.pipeline.try_submit(good, origin));
		fixture.pipeline.advance(origin);
		auto result = fixture.pipeline.poll_result();
		REQUIRE(result);
		CHECK_FALSE(result->failure);
		result.reset();
		auto bad = fixture.frame(2);
		REQUIRE_FALSE(fixture.pipeline.try_submit(bad, origin));
		fixture.pipeline.advance(origin);
		result = fixture.pipeline.poll_result();
		REQUIRE(result);
		const auto expected = fault == FakeFault::out_of_memory    ? ErrorCode::out_of_memory
				      : fault == FakeFault::invalid_output ? ErrorCode::invalid_output
									   : ErrorCode::backend_unavailable;
		CHECK(result->failure.code == expected);
		CHECK_FALSE(presentable(*result, 42, 1, 0, origin, 250ms));
		CHECK_FALSE(error_id(result->failure.code).empty());
	}
}

TEST_CASE("unavailable prepare retains the working generation")
{
	Fixture fixture;
	fixture.pipeline.configure({0ms, 0ms, FakeFault::backend_unavailable});
	PrepareRequest replacement;
	replacement.instance = 42;
	replacement.generation = 2;
	replacement.settings = fixture.settings;
	CHECK(fixture.pipeline.prepare(replacement).code == ErrorCode::backend_unavailable);
	CHECK(fixture.pipeline.diagnostics().generation == 1);
	auto input = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
	fixture.pipeline.advance(origin);
	auto output = fixture.pipeline.poll_result();
	REQUIRE(output);
	CHECK_FALSE(output->failure);
}

TEST_CASE("global and per-frame cancellation interrupts simulated delay")
{
	for (bool global : {false, true}) {
		Fixture fixture({2000ms});
		std::stop_source source;
		auto input = fixture.frame(1);
		input.cancellation = source.get_token();
		REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
		if (global)
			fixture.pipeline.cancel();
		else
			source.request_stop();
		fixture.pipeline.advance(origin + 1ms);
		auto output = fixture.pipeline.poll_result();
		REQUIRE(output);
		CHECK(output->failure.code == ErrorCode::cancelled);
		output.reset();
		CHECK(fixture.pool.in_use() == 0);
	}
}

TEST_CASE("cancellation also invalidates a completed but unconsumed result")
{
	Fixture fixture;
	auto input = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
	fixture.pipeline.advance(origin);
	fixture.pipeline.cancel();
	auto output = fixture.pipeline.poll_result();
	REQUIRE(output);
	CHECK(output->failure.code == ErrorCode::cancelled);
	CHECK(fixture.pipeline.diagnostics().lifecycle == Lifecycle::stopping);
}

TEST_CASE("result validation detects mismatched origin and nonfinite foreground")
{
	Fixture fixture;
	auto input = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
	fixture.pipeline.advance(origin);
	auto output = fixture.pipeline.poll_result();
	REQUIRE(output);
	output->identity.captured_at += 1ns;
	CHECK(validate_result(*output).code == ErrorCode::stale_generation);
	output->identity = output->input.identity;
	std::vector<float> values(17 * 9 * 3, 0.5f);
	values.back() = std::numeric_limits<float>::infinity();
	output->foreground.storage = std::make_shared<const HostFloats>(std::move(values));
	CHECK(validate_result(*output).code == ErrorCode::invalid_output);
}

TEST_CASE("rapid generation swaps with a worker retain bounded ownership")
{
	Fixture fixture({2ms}, FakeExecution::worker);
	for (unsigned iteration = 0; iteration < 200; ++iteration) {
		auto input = fixture.frame(1, Clock::now());
		const auto result = fixture.pipeline.try_submit(input, Clock::now());
		CHECK((!result || result.code == ErrorCode::busy));
		CHECK_FALSE(fixture.pipeline.reset(fixture.pool.advance_generation(), ResetReason::source_changed));
		CHECK(fixture.pool.in_use() <= FramePool::capacity);
	}
	fixture.pipeline.shutdown();
	CHECK(fixture.pool.in_use() == 0);
	CHECK(fixture.pipeline.diagnostics().outstanding == 0);
}

TEST_CASE("duplicate ticks and malformed inputs preserve caller ownership")
{
	Fixture fixture;
	auto first = fixture.frame(1);
	REQUIRE_FALSE(fixture.pipeline.try_submit(first, origin));
	auto duplicate = fixture.frame(2);
	duplicate.identity.video_tick = 10;
	CHECK(fixture.pipeline.try_submit(duplicate, origin).code == ErrorCode::invalid_input);
	CHECK(duplicate.lease.state() == FrameState::ready);
	duplicate.identity.video_tick = 20;
	duplicate.transform.user_crop.right = std::numeric_limits<float>::quiet_NaN();
	CHECK(fixture.pipeline.try_submit(duplicate, origin).code == ErrorCode::invalid_input);
	duplicate.transform.user_crop = {};
	duplicate.analysis.color = ColorSpace::hdr_unsupported;
	CHECK(fixture.pipeline.try_submit(duplicate, origin).code == ErrorCode::unsupported);
}

TEST_CASE("unsignalled capture is never consumed or released by the fake")
{
	Fixture fixture;
	auto input = fixture.frame(1);
	auto fence = std::make_shared<Fence>();
	input.capture_complete = fence;
	CHECK(fixture.pipeline.try_submit(input, origin).code == ErrorCode::busy);
	CHECK(static_cast<bool>(input.lease));
	fence->signalled.store(true);
	REQUIRE_FALSE(fixture.pipeline.try_submit(input, origin));
	fixture.pipeline.advance(origin);
	auto output = fixture.pipeline.poll_result();
	REQUIRE(output);
	auto output_fence = std::make_shared<Fence>();
	output->output_complete = output_fence;
	CHECK_FALSE(presentable(*output, 42, 1, 0, origin, 250ms));
	output_fence->signalled.store(true);
	CHECK(presentable(*output, 42, 1, 0, origin, 250ms));
}

TEST_CASE("two instances keep independent state and immutable settings")
{
	Fixture first;
	Fixture second;
	auto customized = std::make_shared<Settings>();
	customized->context.crop = {0, 0, 0.5f, 1};
	first.settings = customized;
	auto a = first.frame(1);
	auto b = second.frame(1);
	REQUIRE_FALSE(first.pipeline.try_submit(a, origin));
	REQUIRE_FALSE(second.pipeline.try_submit(b, origin));
	first.pipeline.cancel();
	first.pipeline.advance(origin);
	second.pipeline.advance(origin);
	auto cancelled = first.pipeline.poll_result();
	auto ready = second.pipeline.poll_result();
	REQUIRE(cancelled);
	REQUIRE(ready);
	CHECK(cancelled->failure.code == ErrorCode::cancelled);
	CHECK_FALSE(ready->failure);
	CHECK(ready->input.settings->context.crop.right == 1);
}

TEST_CASE("real worker completes, stops without waiting for fake delay, and can be destroyed in flight")
{
	Fixture fixture({2ms}, FakeExecution::worker);
	const auto now = Clock::now();
	auto input = fixture.frame(1, now);
	Error submitted;
	const auto deadline = now + 2s;
	do {
		submitted = fixture.pipeline.try_submit(input, Clock::now());
		if (submitted)
			std::this_thread::yield();
	} while (submitted.code == ErrorCode::busy && Clock::now() < deadline);
	REQUIRE_FALSE(submitted);
	std::optional<FrameResult> result;
	while (!(result = fixture.pipeline.poll_result()) && Clock::now() < deadline)
		std::this_thread::sleep_for(1ms);
	REQUIRE(result);
	CHECK_FALSE(validate_result(*result));
	result.reset();
	fixture.pipeline.configure({2000ms});
	REQUIRE_FALSE(fixture.pipeline.reset(fixture.pool.advance_generation(), ResetReason::explicit_reset));
	input = fixture.frame(2, Clock::now());
	do {
		submitted = fixture.pipeline.try_submit(input, Clock::now());
	} while (submitted.code == ErrorCode::busy && Clock::now() < deadline);
	REQUIRE_FALSE(submitted);
	const auto before = Clock::now();
	fixture.pipeline.shutdown();
	CHECK(Clock::now() - before < 1s);
	CHECK(fixture.pool.in_use() == 0);
	CHECK(fixture.pipeline.diagnostics().lifecycle == Lifecycle::stopped);
}
