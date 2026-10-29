// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "reconstruction.hpp"
#include "engine/refiner_stage.hpp"
#include "obs/gpu_fence.hpp"

namespace corridorkey {
// Graphics-thread resources. A caller pins the capture and Reconstruction until finish/cancel completes.
class TileRefiner {
public:
	TileRefiner(Extent native, unsigned budget, const char *effect_path, bool interop = false);
	~TileRefiner();
	bool available() const noexcept { return valid_; }
	bool begin(Reconstruction &, gs_texture_t *native, gs_texture_t *model, const Crop &roi);
	// The fence must be ready before mapping. No wait or inference occurs here.
	bool read_ready() const { return fence_.ready(); }
	bool failed() const { return fence_.failed(); }
	std::vector<float> read_atlas();
	bool blend(std::span<const float> centres);
	bool blend_device();
	gs_texture_t *centres() const { return centres_; }
	bool interop() const { return interop_; }
	gs_texture_t *raw() const;
	gs_texture_t *atlas() const;
	gs_texture_t *selection() const;
	gs_texture_t *scores() const;
	std::size_t readback_bytes() const;

private:
	gs_texrender_t *allocate(Extent);
	bool draw(gs_texrender_t *, Extent, const char *);
	Extent native_, cells_, sort_, atlas_, output_;
	unsigned budget_, sort_count_ = 1, sort_stride_ = 1, sort_block_ = 2;
	Crop roi_;
	std::array<float, 4> roi_pixels_{};
	Reconstruction *post_ = nullptr;
	gs_texture_t *native_texture_ = nullptr, *model_ = nullptr, *sort_input_ = nullptr, *sorted_ = nullptr;
	gs_effect_t *effect_ = nullptr;
	gs_texrender_t *score_ = nullptr, *sort_a_ = nullptr, *sort_b_ = nullptr, *map_ = nullptr, *gather_ = nullptr,
		       *blend_ = nullptr;
	gs_texture_t *centres_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
	GpuFence fence_;
	std::vector<gs_texrender_t *> owned_;
	bool valid_ = false;
	bool interop_ = false;
};
} // namespace corridorkey
