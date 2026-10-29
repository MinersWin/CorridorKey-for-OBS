// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include <doctest/doctest.h>
#include "runtime/scheduler.hpp"
#include "engine/fake_pipeline.hpp"

using namespace corridorkey;
namespace {
InputFrame frame(FramePool &pool, Sequence sequence, std::uint64_t tick, TimePoint now)
{
	InputFrame result;
	auto lease = pool.try_acquire();
	REQUIRE(lease.has_value());
	result.lease = std::move(*lease);
	REQUIRE(result.lease.transition(FrameState::ready));
	result.identity = {1, pool.generation(), sequence, tick, now};
	result.original = result.analysis = synthetic_rgb({4, 4});
	result.transform.source_size = {4, 4};
	result.settings = std::make_shared<Settings>();
	return result;
}
void prepare(FakePipeline &pipeline, Generation generation)
{
	PrepareRequest request;
	request.instance = 1;
	request.generation = generation;
	request.settings = std::make_shared<Settings>();
	REQUIRE_FALSE(pipeline.prepare(request));
}
} // namespace
TEST_CASE("duplicate views and reset in one tick cannot submit twice")
{
	FairAdmission gate;
	Scheduler scheduler(gate);
	FramePool pool;
	FakePipeline pipeline({}, FakeExecution::manual);
	prepare(pipeline, pool.generation());
	const auto now = Clock::now();
	CHECK(scheduler.begin_tick(42));
	CHECK_FALSE(scheduler.begin_tick(42));
	scheduler.offer(frame(pool, 1, 42, now));
	CHECK_FALSE(scheduler.submit(pipeline, 42, now));
	pipeline.advance(now);
	auto result = pipeline.poll_result();
	REQUIRE(result);
	CHECK(result->identity == result->input.identity);
	result.reset();
	scheduler.completed();
	scheduler.reset();
	CHECK_FALSE(scheduler.begin_tick(42));
	pool.advance_generation();
	prepare(pipeline, pool.generation());
	scheduler.offer(frame(pool, 2, 42, now));
	CHECK(scheduler.submit(pipeline, 42, now).code == ErrorCode::busy);
	CHECK_FALSE(scheduler.submit(pipeline, 43, now));
}
TEST_CASE("slow processing retains one running and the newest pending frame")
{
	FairAdmission gate;
	Scheduler scheduler(gate);
	FramePool pool;
	FakeOptions options;
	options.delay = std::chrono::milliseconds(100);
	FakePipeline pipeline(options, FakeExecution::manual);
	prepare(pipeline, pool.generation());
	const auto now = Clock::now();
	scheduler.offer(frame(pool, 1, 1, now));
	CHECK_FALSE(scheduler.submit(pipeline, 1, now));
	scheduler.offer(frame(pool, 2, 2, now));
	scheduler.offer(frame(pool, 3, 3, now));
	CHECK(scheduler.dropped() == 1);
	CHECK(pool.in_use() == 2);
	CHECK(scheduler.submit(pipeline, 3, now).code == ErrorCode::busy);
	pipeline.advance(now + std::chrono::milliseconds(100));
	auto result = pipeline.poll_result();
	REQUIRE(result);
	CHECK(result->identity.sequence == 1);
	scheduler.completed();
	result.reset();
	CHECK_FALSE(scheduler.submit(pipeline, 4, now + std::chrono::milliseconds(100)));
	pipeline.advance(now + std::chrono::milliseconds(200));
	result = pipeline.poll_result();
	REQUIRE(result);
	CHECK(result->identity.sequence == 3);
	CHECK(result->input.identity.video_tick == 3);
	CHECK(result->input.identity.captured_at == now);
}
TEST_CASE("device admission is fair and cancelled waiters do not starve peers")
{
	FairAdmission gate;
	const auto a = gate.attach(), b = gate.attach(), c = gate.attach();
	CHECK(gate.acquire(a));
	CHECK_FALSE(gate.acquire(b));
	CHECK_FALSE(gate.acquire(c));
	gate.release(a);
	CHECK_FALSE(gate.acquire(a));
	CHECK(gate.acquire(b));
	gate.release(b);
	gate.withdraw(c);
	CHECK(gate.acquire(a));
	gate.detach(a);
	gate.detach(b);
	gate.detach(c);
}
TEST_CASE("generation invalidation leaves running storage pinned until reset")
{
	FairAdmission gate;
	Scheduler scheduler(gate);
	FramePool pool;
	FakeOptions options;
	options.delay = std::chrono::seconds(1);
	FakePipeline pipeline(options, FakeExecution::manual);
	prepare(pipeline, pool.generation());
	const auto now = Clock::now();
	scheduler.offer(frame(pool, 1, 1, now));
	CHECK_FALSE(scheduler.submit(pipeline, 1, now));
	pool.advance_generation();
	CHECK(pool.in_use() == 1);
	scheduler.reset();
	prepare(pipeline, pool.generation());
	CHECK(pool.in_use() == 0);
	CHECK_FALSE(pipeline.poll_result());
}
TEST_CASE("late staging completion cannot replace a newer pending capture")
{
	FairAdmission gate;
	Scheduler scheduler(gate);
	FramePool pool;
	FakePipeline pipeline({}, FakeExecution::manual);
	prepare(pipeline, pool.generation());
	const auto now = Clock::now();
	scheduler.offer(frame(pool, 3, 3, now));
	scheduler.offer(frame(pool, 2, 2, now));
	CHECK(scheduler.dropped() == 1);
	CHECK(pool.in_use() == 1);
	CHECK_FALSE(scheduler.submit(pipeline, 4, now));
	pipeline.advance(now);
	auto result = pipeline.poll_result();
	REQUIRE(result);
	CHECK(result->identity.sequence == 3);
}
