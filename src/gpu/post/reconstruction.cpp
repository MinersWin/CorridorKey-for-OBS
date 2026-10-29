// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "reconstruction.hpp"
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <algorithm>
#include <cmath>

namespace corridorkey {
namespace {
float finite(float value, float fallback)
{
	return std::isfinite(value) ? value : fallback;
}
struct DrawState {
	bool srgb = gs_framebuffer_srgb_enabled();
	DrawState()
	{
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		gs_enable_framebuffer_srgb(false);
	}
	~DrawState()
	{
		gs_enable_framebuffer_srgb(srgb);
		gs_blend_state_pop();
	}
};
} // namespace
Reconstruction::Reconstruction(Extent native, Extent grid, const char *path) : native_(native), grid_(grid)
{
	owned_.reserve(64);
	levels_.reserve(32);
	pull_.reserve(32);
	push_.reserve(32);
	values_.reserve(32);
	textures_.reserve(16);
	effect_ = gs_effect_create_from_file(path, nullptr);
	if (!effect_ || !native.width || !native.height || !grid.width || !grid.height)
		return;
	size(grid);
	vec2 grid_dimensions;
	vec2_set(&grid_dimensions, static_cast<float>(grid.width), static_cast<float>(grid.height));
	set("grid_size", grid_dimensions);
	set("coarse_size", grid_dimensions);
	crop("roi", {});
	crop("user_cut", {});
	vec4 defaults;
	vec4_set(&defaults, 1, 0, 0, 1);
	set("finish_params", defaults);
	set("reconstruction_params", defaults);
	set("radius", 2);
	set("view", 0);
	set("band_low", 0.004f);
	for (const char *name : {"linear_output", "use_refined", "equation_unmix", "bilinear_alpha"})
		set(name, 0);
	solve_ = allocate(grid);
	coefficients_ = allocate(grid);
	delta_ = allocate(grid);
	band_raw_ = allocate(grid);
	band_ = allocate(grid);
	raw_ = allocate(native);
	reduction_ = allocate({32, 32});
	background_ = allocate(grid);
	for (;;) {
		levels_.push_back(grid);
		pull_.push_back(allocate(grid));
		push_.push_back(allocate(grid));
		if (grid.width == 1 && grid.height == 1)
			break;
		grid = {(grid.width + 1) / 2, (grid.height + 1) / 2};
	}
	valid_ = std::all_of(owned_.begin(), owned_.end(), [](auto *p) { return p && gs_texrender_get_texture(p); });
}
Reconstruction::~Reconstruction()
{
	for (auto *p : owned_)
		gs_texrender_destroy(p);
	gs_effect_destroy(effect_);
}
gs_texrender_t *Reconstruction::allocate(Extent extent)
{
	auto *target = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
	owned_.push_back(target);
	if (target && gs_texrender_begin(target, extent.width, extent.height))
		gs_texrender_end(target);
	return target;
}
gs_eparam_t *Reconstruction::parameter(const char *name) const
{
	return gs_effect_get_param_by_name(effect_, name);
}
void Reconstruction::texture(const char *name, gs_texture_t *value)
{
	auto *p = parameter(name);
	for (auto &entry : textures_)
		if (entry.first == p) {
			entry.second = value;
			return;
		}
	textures_.emplace_back(p, value);
}
void Reconstruction::bind()
{
	for (const auto &entry : values_)
		gs_effect_set_val(entry.parameter, entry.bytes.data(), entry.size);
	for (const auto &entry : textures_)
		gs_effect_set_texture(entry.first, entry.second);
}
void Reconstruction::size(Extent value)
{
	vec2 dimensions, pixel;
	vec2_set(&dimensions, static_cast<float>(value.width), static_cast<float>(value.height));
	vec2_set(&pixel, 1.0f / dimensions.x, 1.0f / dimensions.y);
	set("input_size", dimensions);
	set("texel", pixel);
}
void Reconstruction::crop(const char *name, const Crop &value)
{
	vec4 bounds;
	vec4_set(&bounds, value.left, value.top, value.right, value.bottom);
	set(name, bounds);
}
bool Reconstruction::draw(gs_texrender_t *target, Extent extent, const char *technique)
{
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, extent.width, extent.height))
		return false;
	gs_ortho(0, static_cast<float>(extent.width), 0, static_cast<float>(extent.height), -100, 100);
	bind();
	while (gs_effect_loop(effect_, technique))
		gs_draw_sprite(nullptr, 0, extent.width, extent.height);
	gs_texrender_end(target);
	return true;
}
bool Reconstruction::prepare(gs_texture_t *native, gs_texture_t *guide, gs_texture_t *model, const Settings &settings,
			     const Crop &roi)
{
	if (!valid_ || !native || !guide || !model)
		return false;
	DrawState state;
	const auto &q = settings.quality;
	vec4 params;
	vec4_set(&params, std::clamp(finite(q.guided_epsilon, 0.0005f), 0.00001f, 1.0f),
		 std::clamp(finite(q.unmix_inner, 1), 0.0f, 1.0f),
		 std::clamp(finite(q.despill_strength, 0), 0.0f, 1.0f), finite(q.refiner_band_gain, 10));
	set("reconstruction_params", params);
	set("band_low", std::clamp(finite(q.refiner_band_lo, 0.004f), 0.0f, 0.5f));
	texture("guide_image", guide);
	texture("model_image", model);
	size(grid_);
	if (!draw(reduction_, {32, 32}, "Reduce"))
		return false;
	set("radius", static_cast<int>(std::clamp(q.guided_radius, 1U, 8U)));
	if (!draw(solve_, grid_, "Solve"))
		return false;
	texture("image", gs_texrender_get_texture(solve_));
	if (!draw(coefficients_, grid_, "Box") || !draw(delta_, grid_, "Delta") || !draw(band_raw_, grid_, "Band"))
		return false;
	texture("image", gs_texrender_get_texture(band_raw_));
	set("radius", 1);
	if (!draw(band_, grid_, "Box") || !draw(pull_[0], grid_, "BackgroundSeed"))
		return false;
	for (std::size_t i = 1; i < levels_.size(); ++i) {
		size(levels_[i - 1]);
		texture("image", gs_texrender_get_texture(pull_[i - 1]));
		if (!draw(pull_[i], levels_[i], "Pull"))
			return false;
	}
	texture("image", gs_texrender_get_texture(pull_.back()));
	if (!draw(push_.back(), levels_.back(), "SeedTop"))
		return false;
	for (std::size_t i = levels_.size() - 1; i-- > 0;) {
		texture("image", gs_texrender_get_texture(pull_[i]));
		texture("coarse_image", gs_texrender_get_texture(push_[i + 1]));
		vec2 dimensions;
		vec2_set(&dimensions, static_cast<float>(levels_[i + 1].width),
			 static_cast<float>(levels_[i + 1].height));
		set("coarse_size", dimensions);
		if (!draw(push_[i], levels_[i], "Push"))
			return false;
	}
	texture("image", gs_texrender_get_texture(push_[0]));
	if (!draw(background_, grid_, "EncodeBackground"))
		return false;
	texture("image", native);
	texture("coefficient_image", coefficients());
	texture("delta_image", delta());
	texture("background_image", background());
	crop("roi", roi);
	set("equation_unmix", int(q.unmix_mode == UnmixMode::compositing_equation));
	set("bilinear_alpha", int(q.guide_mode == GuideMode::bilinear));
	return draw(raw_, native_, "Reconstruct");
}
bool Reconstruction::finish(gs_texrender_t *output, gs_texture_t *native, const Settings &settings, const Crop &roi,
			    int debug_view, gs_texture_t *refined)
{
	if (!valid_)
		return false;
	DrawState state;
	const auto &q = settings.quality;
	vec4 params;
	vec4_set(&params, finite(q.alpha_contrast, 1), finite(q.alpha_offset, 0), finite(q.clip_black, 0),
		 finite(q.clip_white, 1));
	set("finish_params", params);
	texture("image", raw());
	texture("guide_image", native);
	texture("band_image", band());
	texture("refined_image", refined ? refined : raw());
	set("use_refined", int(refined != nullptr));
	crop("roi", roi);
	crop("user_cut", settings.context.crop);
	int view = debug_view;
	if (!view)
		switch (settings.context.output) {
		case OutputView::alpha:
			view = 1;
			break;
		case OutputView::foreground:
			view = 2;
			break;
		case OutputView::crop:
		case OutputView::roi:
			view = 5;
			break;
		case OutputView::input:
			view = 6;
			break;
		default:
			break;
		}
	set("view", view);
	return draw(output, native_, "Finish");
}
void Reconstruction::present(gs_texture_t *output)
{
	const bool srgb = gs_framebuffer_srgb_enabled();
	const bool linear = gs_get_linear_srgb();
	gs_enable_framebuffer_srgb(linear);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	texture("image", output);
	set("linear_output", int(linear));
	bind();
	while (gs_effect_loop(effect_, "Present"))
		gs_draw_sprite(output, 0, native_.width, native_.height);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(srgb);
}
gs_texture_t *Reconstruction::raw() const
{
	return gs_texrender_get_texture(raw_);
}
gs_texture_t *Reconstruction::coefficients() const
{
	return gs_texrender_get_texture(coefficients_);
}
gs_texture_t *Reconstruction::delta() const
{
	return gs_texrender_get_texture(delta_);
}
gs_texture_t *Reconstruction::background() const
{
	return gs_texrender_get_texture(background_);
}
gs_texture_t *Reconstruction::band() const
{
	return gs_texrender_get_texture(band_);
}
gs_texture_t *Reconstruction::reduction() const
{
	return gs_texrender_get_texture(reduction_);
}
} // namespace corridorkey
