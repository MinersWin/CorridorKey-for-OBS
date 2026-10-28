// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "frame_pool.hpp"
#include <array>
#include <atomic>
#include <utility>

namespace corridorkey {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<FrameState>::is_always_lock_free);
struct PoolState {
	struct Slot {
		std::atomic<FrameState> state{FrameState::free};
		std::atomic<std::uint64_t> reuse{0};
	};
	std::array<Slot, FramePool::capacity> slots;
	std::atomic<Generation> generation{1};
};

FrameLease::FrameLease(std::shared_ptr<PoolState> pool, SlotId id, Generation generation) noexcept
	: pool_(std::move(pool)),
	  id_(id),
	  generation_(generation)
{
}
FrameLease::FrameLease(FrameLease &&other) noexcept
	: pool_(std::move(other.pool_)),
	  id_(other.id_),
	  generation_(other.generation_)
{
}
FrameLease &FrameLease::operator=(FrameLease &&other) noexcept
{
	if (this != &other) {
		release();
		pool_ = std::move(other.pool_);
		id_ = other.id_;
		generation_ = other.generation_;
	}
	return *this;
}
FrameLease::~FrameLease()
{
	release();
}
bool FrameLease::current() const noexcept
{
	return pool_ && generation_ == pool_->generation.load(std::memory_order_acquire);
}
FrameState FrameLease::state() const noexcept
{
	return pool_ ? pool_->slots[id_.index].state.load(std::memory_order_acquire) : FrameState::free;
}
bool FrameLease::transition(FrameState next) noexcept
{
	if (!pool_)
		return false;
	auto previous = state();
	const bool valid = (previous == FrameState::capturing && next == FrameState::ready) ||
			   (previous == FrameState::ready && next == FrameState::processing) ||
			   (previous == FrameState::processing && next == FrameState::complete) ||
			   (previous == FrameState::complete && next == FrameState::presenting) ||
			   (previous == FrameState::presenting && next == FrameState::retired);
	return valid && pool_->slots[id_.index].state.compare_exchange_strong(previous, next);
}
void FrameLease::release() noexcept
{
	if (pool_) {
		pool_->slots[id_.index].state.store(FrameState::free, std::memory_order_release);
		pool_.reset();
	}
}
FramePool::FramePool() : state_(std::make_shared<PoolState>()) {}
std::optional<FrameLease> FramePool::try_acquire() noexcept
{
	const auto generation = this->generation();
	for (std::uint32_t i = 0; i < capacity; ++i) {
		auto expected = FrameState::free;
		auto &slot = state_->slots[i];
		if (slot.state.compare_exchange_strong(expected, FrameState::capturing)) {
			const auto reuse = slot.reuse.fetch_add(1) + 1;
			return FrameLease(state_, {i, reuse}, generation);
		}
	}
	return std::nullopt;
}
Generation FramePool::advance_generation() noexcept
{
	return state_->generation.fetch_add(1) + 1;
}
Generation FramePool::generation() const noexcept
{
	return state_->generation.load(std::memory_order_acquire);
}
std::size_t FramePool::in_use() const noexcept
{
	std::size_t count = 0;
	for (const auto &slot : state_->slots)
		count += slot.state.load(std::memory_order_acquire) != FrameState::free;
	return count;
}
} // namespace corridorkey
