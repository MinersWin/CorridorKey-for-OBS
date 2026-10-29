// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <cstdint>
namespace corridorkey {
// OBS tick timestamps avoid callback wall-clock jitter at the canvas rate.
class CapturePacer {
public:
	bool due(std::uint64_t tick) const noexcept { return !next_ || tick + rounding_slack >= next_; }
	void captured(std::uint64_t tick, std::uint32_t fps) noexcept
	{
		if (!fps) {
			next_ = 0;
			return;
		}
		const auto period = std::uint64_t{1000000000} / fps;
		if (!next_)
			next_ = tick;
		next_ += ((tick + rounding_slack - next_) / period + 1) * period;
	}
	void reset() noexcept { next_ = 0; }

private:
	static constexpr std::uint64_t rounding_slack = 1000;
	std::uint64_t next_ = 0;
};
} // namespace corridorkey
