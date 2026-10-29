// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/types.hpp"
#include <util/platform.h>

namespace corridorkey {
// TimePoint is a duration carrier here. Never combine this epoch with Clock::now().
inline TimePoint obs_now() noexcept
{
	return TimePoint{std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(os_gettime_ns()))};
}
} // namespace corridorkey
