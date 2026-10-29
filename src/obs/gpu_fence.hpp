// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include <memory>

namespace corridorkey {
// All operations, including destruction, require OBS graphics ownership.
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

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey
