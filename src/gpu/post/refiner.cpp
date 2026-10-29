// SPDX-License-Identifier: GPL-3.0-or-later
#include "refiner.hpp"
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace corridorkey {
TileRefiner::TileRefiner(Extent native, unsigned budget, const char *path, bool interop)
	: native_(native),
	  cells_{(native.width + 31) / 32, (native.height + 31) / 32},
	  atlas_(refiner::input_atlas(budget)),
	  output_(refiner::output_atlas(budget)),
	  budget_(budget)
{
	interop_ = interop;
	if (!native.width || !native.height || native.width > 8192 || native.height > 8192)
		return;
	while (sort_count_ < cells_.width * cells_.height)
		sort_count_ *= 2;
	sort_ = {std::min(128U, sort_count_), (sort_count_ + 127) / 128};
	effect_ = gs_effect_create_from_file(path, nullptr);
	if (!effect_ || !fence_.supported())
		return;
	score_ = allocate(sort_);
	sort_a_ = allocate(sort_);
	sort_b_ = allocate(sort_);
	map_ = allocate(cells_);
	gather_ = allocate(atlas_);
	blend_ = allocate(native_);
	centres_ = gs_texture_create(output_.width, output_.height, GS_RGBA32F, 1, nullptr, interop ? 0 : GS_DYNAMIC);
	stage_ = gs_stagesurface_create(atlas_.width, atlas_.height, GS_RGBA32F);
	valid_ = centres_ && stage_ &&
		 std::all_of(owned_.begin(), owned_.end(), [](auto *p) { return p && gs_texrender_get_texture(p); });
}
TileRefiner::~TileRefiner()
{
	for (auto *p : owned_)
		gs_texrender_destroy(p);
	gs_texture_destroy(centres_);
	gs_stagesurface_destroy(stage_);
	gs_effect_destroy(effect_);
}
gs_texrender_t *TileRefiner::allocate(Extent size)
{
	auto *p = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
	owned_.push_back(p);
	if (p && gs_texrender_begin(p, size.width, size.height))
		gs_texrender_end(p);
	return p;
}
bool TileRefiner::draw(gs_texrender_t *target, Extent size, const char *technique)
{
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, size.width, size.height))
		return false;
	gs_ortho(0, float(size.width), 0, float(size.height), -100, 100);
	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	auto param = [&](const char *name) {
		return gs_effect_get_param_by_name(effect_, name);
	};
	auto texture = [&](const char *name, gs_texture_t *p) {
		gs_effect_set_texture(param(name), p);
	};
	auto dimensions = [&](const char *name, Extent d) {
		vec2 v;
		vec2_set(&v, float(d.width), float(d.height));
		gs_effect_set_vec2(param(name), &v);
	};
	dimensions("native_size", native_);
	dimensions("cell_size", cells_);
	dimensions("sort_size", sort_);
	dimensions("atlas_size", atlas_);
	dimensions("output_size", output_);
	dimensions("grid_size", {gs_texture_get_width(model_), gs_texture_get_height(model_)});
	gs_effect_set_int(param("budget"), int(budget_));
	gs_effect_set_int(param("sort_stride"), int(sort_stride_));
	gs_effect_set_int(param("sort_block"), int(sort_block_));
	vec4 roi;
	vec4_set(&roi, roi_.left, roi_.top, roi_.right, roi_.bottom);
	gs_effect_set_vec4(param("roi"), &roi);
	vec4_set(&roi, roi_pixels_[0], roi_pixels_[1], roi_pixels_[2], roi_pixels_[3]);
	gs_effect_set_vec4(param("roi_pixels"), &roi);
	texture("native_image", native_texture_);
	texture("model_image", model_);
	texture("band_image", post_->band());
	texture("background_image", post_->background());
	texture("raw_image", post_->raw());
	texture("sort_image", sort_input_ ? sort_input_ : gs_texrender_get_texture(score_));
	texture("sorted_image", sorted_ ? sorted_ : gs_texrender_get_texture(score_));
	texture("score_image", gs_texrender_get_texture(score_));
	texture("map_image", gs_texrender_get_texture(map_));
	texture("centres_image", centres_);
	while (gs_effect_loop(effect_, technique))
		gs_draw_sprite(nullptr, 0, size.width, size.height);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(srgb);
	gs_texrender_end(target);
	return true;
}
bool TileRefiner::begin(Reconstruction &post, gs_texture_t *native, gs_texture_t *model, const Crop &roi)
{
	if (!valid_)
		return false;
	post_ = &post;
	native_texture_ = native;
	model_ = model;
	roi_ = roi;
	const unsigned left =
		std::min(native_.width - 1, unsigned(std::max(0.0f, std::round(roi.left * float(native_.width)))));
	const unsigned top =
		std::min(native_.height - 1, unsigned(std::max(0.0f, std::round(roi.top * float(native_.height)))));
	const unsigned right = std::clamp(unsigned(std::max(0.0f, std::round(roi.right * float(native_.width)))),
					  left + 1, native_.width);
	const unsigned bottom = std::clamp(unsigned(std::max(0.0f, std::round(roi.bottom * float(native_.height)))),
					   top + 1, native_.height);
	roi_pixels_ = {float(left), float(top), float(right - left), float(bottom - top)};
	cells_ = {(right - left + 31) / 32, (bottom - top + 31) / 32};
	sort_count_ = 1;
	while (sort_count_ < cells_.width * cells_.height)
		sort_count_ *= 2;
	sort_ = {std::min(128U, sort_count_), (sort_count_ + 127) / 128};
	if (!draw(score_, sort_, "Score"))
		return false;
	sort_input_ = gs_texrender_get_texture(score_);
	bool a = true;
	for (sort_block_ = 2; sort_block_ <= sort_count_; sort_block_ *= 2)
		for (sort_stride_ = sort_block_ / 2; sort_stride_; sort_stride_ /= 2) {
			auto *target = a ? sort_a_ : sort_b_;
			if (!draw(target, sort_, "Sort"))
				return false;
			sort_input_ = gs_texrender_get_texture(target);
			a = !a;
		}
	sorted_ = sort_input_;
	if (!draw(map_, cells_, "Map") || !draw(gather_, atlas_, "Gather"))
		return false;
	if (!interop_)
		gs_stage_texture(stage_, atlas());
	return fence_.signal();
}
std::vector<float> TileRefiner::read_atlas()
{
	if (!fence_.ready() || fence_.failed())
		throw std::runtime_error("Refiner atlas fence is not ready");
	uint8_t *bytes = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(stage_, &bytes, &pitch))
		throw std::runtime_error("Refiner atlas map failed");
	std::vector<float> result;
	try {
		if (!bytes || pitch < atlas_.width * 16)
			throw std::runtime_error("Invalid refiner atlas stride");
		result.resize(static_cast<std::size_t>(atlas_.width) * atlas_.height * 4);
		for (unsigned y = 0; y < atlas_.height; ++y)
			std::memcpy(result.data() + static_cast<std::size_t>(y) * atlas_.width * 4,
				    bytes + static_cast<std::size_t>(y) * pitch, atlas_.width * 16);
	} catch (...) {
		gs_stagesurface_unmap(stage_);
		throw;
	}
	gs_stagesurface_unmap(stage_);
	return result;
}
bool TileRefiner::blend(std::span<const float> centres)
{
	if (centres.size() != static_cast<std::size_t>(output_.width) * output_.height * 4)
		return false;
	gs_texture_set_image(centres_, reinterpret_cast<const uint8_t *>(centres.data()), output_.width * 16, false);
	return draw(blend_, native_, "Blend");
}
bool TileRefiner::blend_device()
{
	return interop_ && draw(blend_, native_, "Blend");
}
gs_texture_t *TileRefiner::raw() const
{
	return gs_texrender_get_texture(blend_);
}
gs_texture_t *TileRefiner::atlas() const
{
	return gs_texrender_get_texture(gather_);
}
gs_texture_t *TileRefiner::selection() const
{
	return sorted_;
}
gs_texture_t *TileRefiner::scores() const
{
	return gs_texrender_get_texture(score_);
}
std::size_t TileRefiner::readback_bytes() const
{
	return static_cast<std::size_t>(atlas_.width) * atlas_.height * 16;
}
} // namespace corridorkey
