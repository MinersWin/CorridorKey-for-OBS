// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace corridorkey {
// Worker-only coordination for CUDA's process-wide stream-capture restrictions.
class GpuActivity {
public:
	enum class Mode { shared, preparation, capture };
	explicit GpuActivity(Mode mode = Mode::shared)
	{
		auto &local = thread();
		if (local.depth) {
			if (mode == Mode::capture && local.mode != Mode::capture)
				throw std::logic_error("Cannot promote CUDA activity to capture");
			++local.depth;
			return;
		}
		auto &state = shared();
		std::unique_lock lock(state.mutex);
		if (mode == Mode::capture) {
			++state.waiting;
			state.changed.wait(lock, [&] { return !state.capturing && state.readers == 0; });
			--state.waiting;
			state.capturing = true;
		} else {
			state.changed.wait(lock, [&] {
				return !state.capturing &&
				       (!state.waiting || (mode == Mode::shared && state.preparers != 0));
			});
			++state.readers;
			state.preparers += mode == Mode::preparation;
		}
		local = {1, mode};
	}
	~GpuActivity()
	{
		auto &local = thread();
		if (--local.depth)
			return;
		auto &state = shared();
		{
			std::lock_guard lock(state.mutex);
			if (local.mode == Mode::capture)
				state.capturing = false;
			else {
				--state.readers;
				state.preparers -= local.mode == Mode::preparation;
			}
		}
		state.changed.notify_all();
	}
	GpuActivity(const GpuActivity &) = delete;
	GpuActivity &operator=(const GpuActivity &) = delete;

private:
	struct State {
		std::mutex mutex;
		std::condition_variable changed;
		unsigned readers = 0, preparers = 0, waiting = 0;
		bool capturing = false;
	};
	struct Thread {
		unsigned depth = 0;
		Mode mode = Mode::shared;
	};
	static State &shared()
	{
		static State state;
		return state;
	}
	static Thread &thread()
	{
		static thread_local Thread state;
		return state;
	}
};
} // namespace corridorkey
