// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
namespace corridorkey::ort {
struct ProcessResult {
	bool started = false, timed_out = false, cancelled = false, crashed = false;
	int exit_code = -1;
};
// Native process creation, no shell. Kills and reaps the helper on deadline/cancel.
ProcessResult run_helper(const fs::path &helper, const fs::path &request, const fs::path &response,
			 std::chrono::milliseconds timeout, std::stop_token);
} // namespace corridorkey::ort
