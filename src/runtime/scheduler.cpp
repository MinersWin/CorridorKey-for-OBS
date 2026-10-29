// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "scheduler.hpp"

namespace corridorkey {
unsigned FairAdmission::attach() noexcept
{
	for (unsigned i = 0; i < capacity; ++i) {
		bool expected = false;
		if (attached_[i].compare_exchange_strong(expected, true))
			return i;
	}
	return capacity;
}
void FairAdmission::detach(unsigned id) noexcept
{
	if (id >= capacity)
		return;
	waiting_[id] = false;
	release(id);
	attached_[id] = false;
}
bool FairAdmission::acquire(unsigned id) noexcept
{
	if (id >= capacity)
		return false;
	waiting_[id] = true;
	const unsigned start = cursor_.load();
	for (unsigned n = 0; n < capacity; ++n) {
		const auto candidate = (start + n) % capacity;
		if (!waiting_[candidate].load() || !attached_[candidate].load())
			continue;
		if (candidate != id)
			return false;
		unsigned empty = capacity;
		if (!owner_.compare_exchange_strong(empty, id))
			return false;
		waiting_[id] = false;
		return true;
	}
	return false;
}
void FairAdmission::release(unsigned id) noexcept
{
	if (id >= capacity || owner_.load() != id)
		return;
	cursor_ = (id + 1) % capacity;
	owner_ = capacity;
}
void FairAdmission::withdraw(unsigned id) noexcept
{
	if (id < capacity)
		waiting_[id] = false;
}
Scheduler::Scheduler(FairAdmission &admission) : admission_(admission), id_(admission.attach()) {}
Scheduler::~Scheduler()
{
	admission_.detach(id_);
}
bool Scheduler::begin_tick(std::uint64_t tick) noexcept
{
	if (captured_ && capture_tick_ == tick)
		return false;
	captured_ = true;
	capture_tick_ = tick;
	return true;
}
void Scheduler::offer(InputFrame input) noexcept
{
	if (!input.lease.current() || input.identity.sequence <= newest_) {
		++dropped_;
		return;
	}
	newest_ = input.identity.sequence;
	if (pending_)
		++dropped_;
	pending_ = std::move(input);
}
Error Scheduler::submit(Pipeline &pipeline, std::uint64_t tick, TimePoint now) noexcept
{
	if (running_ || !pending_ || (submitted_ && tick == submit_tick_))
		return error(ErrorCode::busy);
	if (!admission_.acquire(id_))
		return error(ErrorCode::busy);
	auto failure = pipeline.try_submit(*pending_, now);
	if (failure) {
		admission_.release(id_);
		if (failure.code != ErrorCode::busy) {
			pending_.reset();
			++dropped_;
		}
		return failure;
	}
	pending_.reset();
	submit_tick_ = tick;
	submitted_ = running_ = true;
	return {};
}
void Scheduler::completed() noexcept
{
	running_ = false;
	admission_.release(id_);
}
void Scheduler::reset() noexcept
{
	if (pending_)
		++dropped_;
	pending_.reset();
	newest_ = 0;
	admission_.withdraw(id_);
	// Cancellation is cooperative. The caller releases admission with completed()
	// only after polling the old job or synchronously draining the pipeline.
	// A reset within one video tick must not admit a second capture/submission.
}
} // namespace corridorkey
