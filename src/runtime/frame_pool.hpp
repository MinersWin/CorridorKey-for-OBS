// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "contracts/types.hpp"
#include <cstddef>
#include <memory>
#include <optional>

namespace corridorkey {
enum class FrameState { free, capturing, ready, processing, complete, presenting, retired };
struct SlotId {
	std::uint32_t index = 0;
	std::uint64_t reuse = 0;
	bool operator==(const SlotId &) const = default;
};
struct PoolState;

// Exclusive transferable ownership; GPU fences must finish before release.
class FrameLease {
public:
	FrameLease() = default;
	FrameLease(const FrameLease &) = delete;
	FrameLease &operator=(const FrameLease &) = delete;
	FrameLease(FrameLease &&other) noexcept;
	FrameLease &operator=(FrameLease &&other) noexcept;
	~FrameLease();
	explicit operator bool() const noexcept { return static_cast<bool>(pool_); }
	SlotId id() const noexcept { return id_; }
	Generation generation() const noexcept { return generation_; }
	bool current() const noexcept;
	FrameState state() const noexcept;
	bool transition(FrameState next) noexcept;
	void release() noexcept;

private:
	friend class FramePool;
	FrameLease(std::shared_ptr<PoolState> pool, SlotId id, Generation generation) noexcept;
	std::shared_ptr<PoolState> pool_;
	SlotId id_;
	Generation generation_ = 0;
};

class FramePool {
public:
	static constexpr std::size_t capacity = 3;
	FramePool();
	FramePool(const FramePool &) = delete;
	FramePool &operator=(const FramePool &) = delete;
	std::optional<FrameLease> try_acquire() noexcept;
	Generation advance_generation() noexcept;
	Generation generation() const noexcept;
	std::size_t in_use() const noexcept;

private:
	std::shared_ptr<PoolState> state_;
};
} // namespace corridorkey
