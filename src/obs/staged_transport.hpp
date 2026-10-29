// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/capabilities.hpp"
#include "gpu_fence.hpp"
#include "gpu/post/reconstruction.hpp"
#include "gpu/post/refiner.hpp"
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
	// Graphics task only. Mapping requires a ready capture fence.
	std::optional<InputFrame> take_ready();
	void discard_stale();
	bool upload(const FrameResult &result, const std::vector<float> &rgba, int debug_view = 0,
		    std::shared_ptr<refiner::Stage> refinement = {});
	bool refining() const noexcept { return refine_phase_ != 0; }
	bool read_finished(const FrameResult &result) const;
	// No graphics access; the input lease pins the byte buffer during conversion.
	Image analysis(const InputFrame &input, nvidia::TextureTransfers *transfers = nullptr,
		       std::shared_ptr<nvidia::Context> context = {});
	bool upload_device(FrameResult &, nvidia::TextureTransfers &);
	void release_analysis(FrameResult &);
	TransportKind transport_kind() const noexcept
	{
		return interop_ ? TransportKind::interop : TransportKind::staged_grid;
	}
	std::uint64_t discarded() const noexcept { return discarded_; }
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
	std::array<std::unique_ptr<Slot>, FramePool::maximum_capacity> slots_;
	std::unique_ptr<Reconstruction> reconstruction_;
	std::unique_ptr<Reconstruction> reconstruction_half_;
	std::unique_ptr<TileRefiner> tiles_;
	std::shared_ptr<refiner::Stage> refine_stage_;
	std::vector<float> atlas_pixels_;
	unsigned refine_phase_ = 0, refine_budget_ = 0;
	Sequence refine_sequence_ = 0;
	std::uint64_t refine_frames_ = 0;
	TimePoint refine_started_{};
	double refine_gather_ms_ = 0, refine_read_ms_ = 0;
	bool available_ = false;
	Error failure_;
	Duration host_draw_time_{};
	std::uint64_t discarded_ = 0;
	std::atomic<bool> interop_{false}, interop_failed_{false};
	std::atomic<bool> interop_requested_{true};
	bool refiner_interop_failed_ = false;
};
} // namespace corridorkey
