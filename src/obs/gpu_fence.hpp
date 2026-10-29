// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <memory>

namespace corridorkey {
// Creation, signaling and query polling require graphics ownership.
// A supported event wait belongs to a worker; the caller pins the fence until it returns.
class GpuFence {
public:
	GpuFence();
	~GpuFence();
	GpuFence(const GpuFence &) = delete;
	GpuFence &operator=(const GpuFence &) = delete;
	bool supported() const noexcept;
	bool signal() noexcept;
	bool ready() const noexcept;
	bool failed() const noexcept;
	bool can_wait() const noexcept;
	bool wait_ready(unsigned timeout_ms = 1000) const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey
