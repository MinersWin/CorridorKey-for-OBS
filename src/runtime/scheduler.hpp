// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "engine/pipeline.hpp"
#include <array>
#include <atomic>

namespace corridorkey {
// One owner per device; waiting instances are visited in registration order.
class FairAdmission {
public:
	static constexpr unsigned capacity = 64;
	unsigned attach() noexcept;
	void detach(unsigned id) noexcept;
	bool acquire(unsigned id) noexcept;
	void release(unsigned id) noexcept;
	void withdraw(unsigned id) noexcept;

private:
	std::array<std::atomic<bool>, capacity> attached_{}, waiting_{};
	std::atomic<unsigned> owner_{capacity}, cursor_{0};
};

class Scheduler {
public:
	explicit Scheduler(FairAdmission &admission);
	~Scheduler();
	Scheduler(const Scheduler &) = delete;
	Scheduler &operator=(const Scheduler &) = delete;
	bool begin_tick(std::uint64_t tick) noexcept;
	void offer(InputFrame input) noexcept;
	Error submit(Pipeline &pipeline, std::uint64_t tick, TimePoint now) noexcept;
	void completed() noexcept;
	void reset() noexcept;
	std::uint64_t dropped() const noexcept { return dropped_; }
	bool running() const noexcept { return running_; }
	bool pending() const noexcept { return pending_.has_value(); }

private:
	FairAdmission &admission_;
	unsigned id_;
	std::optional<InputFrame> pending_;
	std::uint64_t capture_tick_ = 0, submit_tick_ = 0, dropped_ = 0;
	Sequence newest_ = 0;
	bool captured_ = false, submitted_ = false, running_ = false;
};
} // namespace corridorkey
