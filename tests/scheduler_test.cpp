// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include <doctest/doctest.h>
#include "runtime/scheduler.hpp"
#include "runtime/preparation_gate.hpp"
#include "runtime/gpu_activity.hpp"
#include "engine/fake_pipeline.hpp"
#include <barrier>
#include <thread>
#include <vector>

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
	scheduler.completed();
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

TEST_CASE("cancelling one instance keeps admission until its in-flight job drains")
{
	FairAdmission gate;
	Scheduler a(gate), b(gate);
	FramePool pool_a, pool_b;
	FakeOptions options;
	options.delay = std::chrono::seconds(1);
	FakePipeline first(options, FakeExecution::manual), second({}, FakeExecution::manual);
	prepare(first, pool_a.generation());
	prepare(second, pool_b.generation());
	const auto now = Clock::now();
	a.offer(frame(pool_a, 1, 1, now));
	b.offer(frame(pool_b, 1, 1, now));
	REQUIRE_FALSE(a.submit(first, 1, now));
	CHECK(b.submit(second, 1, now).code == ErrorCode::busy);
	pool_a.advance_generation();
	first.cancel();
	a.reset();
	CHECK(a.running());
	CHECK(b.submit(second, 2, now).code == ErrorCode::busy);
	CHECK(pool_a.in_use() == 1);
	first.advance(now);
	auto old = first.poll_result();
	CHECK_FALSE(old); // The fake discards an invalidated generation before publication.
	CHECK(first.diagnostics().outstanding == 0);
	a.completed();
	REQUIRE_FALSE(b.submit(second, 3, now));
	old.reset();
	CHECK(pool_a.in_use() == 0);
	CHECK(pool_b.in_use() == 1);
}

TEST_CASE("admission and independent pools survive concurrent registration and generation churn")
{
	FairAdmission gate;
	constexpr unsigned count = 8, rounds = 2000;
	std::barrier start(count);
	std::atomic<unsigned> inside{0}, violations{0}, finished{0};
	std::vector<std::thread> workers;
	for (unsigned i = 0; i < count; ++i)
		workers.emplace_back([&] {
			FramePool pool(FramePool::maximum_capacity);
			const auto id = gate.attach();
			start.arrive_and_wait();
			for (unsigned n = 0; n < rounds; ++n) {
				while (!gate.acquire(id))
					std::this_thread::yield();
				if (inside.fetch_add(1) != 0)
					++violations;
				auto lease = pool.try_acquire();
				if (!lease || !lease->current())
					++violations;
				pool.advance_generation();
				if (!lease || lease->current() || pool.in_use() != 1)
					++violations;
				lease.reset();
				if (pool.in_use())
					++violations;
				--inside;
				gate.release(id);
			}
			gate.detach(id);
			++finished;
		});
	for (auto &worker : workers)
		worker.join();
	CHECK(finished == count);
	CHECK(violations == 0);
}

TEST_CASE("a full admission registry recovers slots without admitting invalid clients")
{
	FairAdmission gate;
	std::array<unsigned, FairAdmission::capacity> ids{};
	for (auto &id : ids)
		id = gate.attach();
	CHECK(gate.attach() == FairAdmission::capacity);
	CHECK_FALSE(gate.acquire(FairAdmission::capacity));
	gate.detach(ids[17]);
	CHECK(gate.attach() == ids[17]);
	for (auto id : ids)
		gate.detach(id);
}

TEST_CASE("same graph preparation serializes and cancellation releases its waiter")
{
	PreparationGate gate;
	std::stop_source stop;
	std::atomic<bool> entered{false}, cancelled{false};
	std::thread waiter;
	{
		auto first = gate.acquire("graph-a", {});
		REQUIRE(bool(first));
		auto other = gate.acquire("graph-b", {});
		CHECK(bool(other));
		waiter = std::thread([&] {
			entered = true;
			auto second = gate.acquire("graph-a", stop.get_token());
			cancelled = !second;
		});
		while (!entered)
			std::this_thread::yield();
		stop.request_stop();
		waiter.join();
		CHECK(cancelled);
	}
	auto again = gate.acquire("graph-a", {});
	CHECK(bool(again));
}

TEST_CASE("shared frame pool never reuses a pinned slot during concurrent resets")
{
	FramePool pool(FramePool::maximum_capacity);
	std::array<std::atomic<bool>, FramePool::maximum_capacity> held{};
	std::atomic<unsigned> collisions{0};
	std::vector<std::thread> workers;
	for (unsigned i = 0; i < 8; ++i)
		workers.emplace_back([&] {
			for (unsigned n = 0; n < 2000; ++n) {
				std::optional<FrameLease> lease;
				while (!(lease = pool.try_acquire()))
					std::this_thread::yield();
				auto &slot = held[lease->id().index];
				if (slot.exchange(true))
					++collisions;
				pool.advance_generation();
				for (auto state : {FrameState::ready, FrameState::processing, FrameState::complete,
						   FrameState::presenting, FrameState::retired})
					if (!lease->transition(state))
						++collisions;
				slot = false;
			}
		});
	for (auto &worker : workers)
		worker.join();
	CHECK(collisions == 0);
	CHECK(pool.in_use() == 0);
}

TEST_CASE("CUDA capture excludes other workers while nested activity stays safe")
{
	std::atomic<unsigned> readers{0}, writers{0}, violations{0};
	std::vector<std::thread> workers;
	for (unsigned i = 0; i < 8; ++i)
		workers.emplace_back([&, i] {
			for (unsigned n = 0; n < 200; ++n) {
				const bool capture = (n + i) % 7 == 0;
				GpuActivity activity(capture ? GpuActivity::Mode::capture : GpuActivity::Mode::shared);
				GpuActivity nested;
				if (capture) {
					if (writers.fetch_add(1) != 0 || readers != 0)
						++violations;
					std::this_thread::yield();
					--writers;
				} else {
					++readers;
					if (writers != 0)
						++violations;
					std::this_thread::yield();
					--readers;
				}
			}
		});
	for (auto &worker : workers)
		worker.join();
	CHECK(violations == 0);
	GpuActivity activity;
	CHECK_THROWS_AS((GpuActivity{GpuActivity::Mode::capture}), std::logic_error);
}

TEST_CASE("waiting capture does not pause live CUDA calls throughout slow session construction")
{
	auto preparing = std::make_unique<GpuActivity>(GpuActivity::Mode::preparation);
	std::atomic<bool> started{false}, captured{false}, read{false};
	std::thread capture([&] {
		started = true;
		GpuActivity activity(GpuActivity::Mode::capture);
		captured = true;
	});
	while (!started)
		std::this_thread::yield();
	std::thread reader([&] {
		GpuActivity activity;
		read = true;
	});
	const auto deadline = Clock::now() + std::chrono::seconds(2);
	while (!read && Clock::now() < deadline)
		std::this_thread::yield();
	CHECK(read);
	CHECK_FALSE(captured);
	preparing.reset();
	reader.join();
	capture.join();
	CHECK(captured);
}
