// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/capabilities.hpp"
#include "gpu_fence.hpp"
#include <obs.h>
#include <array>
#include <optional>

namespace corridorkey {
class StagedTransport final : public FrameTransport {
public:
	StagedTransport(obs_source_t *source, Extent native, Extent grid);
	~StagedTransport();
	TransportCapabilities capabilities() const override;
	Error try_capture(InputFrame &input) noexcept override;
	bool capture_ready(const InputFrame &input) noexcept override;
	Error try_present(const FrameResult &result,
			  std::shared_ptr<const Completion> &read_complete) noexcept override;
	// Graphics task only. Mapping requires a later tick and a ready capture fence.
	std::optional<InputFrame> take_ready(std::uint64_t tick);
	void discard_stale();
	bool upload(const FrameResult &result, const std::vector<std::uint8_t> &rgba);
	bool read_finished(const FrameResult &result) const;
	// No graphics access; the input lease pins the byte buffer during conversion.
	Image analysis(const InputFrame &input) const;
	bool available() const noexcept { return available_; }
	Error failure() const noexcept { return failure_; }
	Extent native_size() const noexcept { return native_; }
	Extent grid_size() const noexcept { return grid_; }
	Duration host_draw_time() const noexcept { return host_draw_time_; }

private:
	struct Slot;
	obs_source_t *source_;
	Extent native_, grid_;
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *image_ = nullptr, *original_ = nullptr, *result_ = nullptr, *crop_ = nullptr;
	std::array<std::unique_ptr<Slot>, FramePool::capacity> slots_;
	bool available_ = false;
	Error failure_;
	Duration host_draw_time_{};
};
} // namespace corridorkey
