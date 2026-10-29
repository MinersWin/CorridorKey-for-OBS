// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

namespace corridorkey {
// Control/loader threads only. A cancelled waiter never starts another build.
class PreparationGate {
public:
	class Lease {
	public:
		Lease(std::shared_ptr<std::timed_mutex> mutex, std::stop_token stop)
			: mutex_(std::move(mutex)),
			  lock_(*mutex_, std::defer_lock)
		{
			while (!stop.stop_requested())
				if (lock_.try_lock_for(std::chrono::milliseconds(25)))
					break;
			if (stop.stop_requested() && lock_.owns_lock())
				lock_.unlock();
		}
		explicit operator bool() const noexcept { return lock_.owns_lock(); }

	private:
		std::shared_ptr<std::timed_mutex> mutex_;
		std::unique_lock<std::timed_mutex> lock_;
	};
	Lease acquire(const std::string &key, std::stop_token stop)
	{
		std::shared_ptr<std::timed_mutex> entry;
		{
			std::lock_guard lock(mutex_);
			std::erase_if(entries_, [](const auto &pair) { return pair.second.expired(); });
			entry = entries_[key].lock();
			if (!entry) {
				entry = std::make_shared<std::timed_mutex>();
				entries_[key] = entry;
			}
		}
		return Lease(std::move(entry), stop);
	}

private:
	std::mutex mutex_;
	std::map<std::string, std::weak_ptr<std::timed_mutex>> entries_;
};
} // namespace corridorkey
