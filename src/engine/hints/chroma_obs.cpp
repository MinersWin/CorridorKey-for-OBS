// SPDX-License-Identifier: GPL-3.0-or-later
#include "chroma_obs.hpp"
#include <obs-module.h>
#include <cstring>
#include <stdexcept>

namespace corridorkey::ck1 {
namespace {
struct Graphics {
	Graphics() { obs_enter_graphics(); }
	~Graphics() { obs_leave_graphics(); }
};
struct Scene {
	Scene() { gs_begin_scene(); }
	~Scene() { gs_end_scene(); }
};
class Chroma {
public:
	Chroma(Extent grid, const char *path) : grid_(grid), pixels_(grid.width * grid.height * 4ULL)
	{
		Graphics lock;
		if (path)
			effect_ = gs_effect_create_from_file(path, nullptr);
		input_ = gs_texture_create(grid.width, grid.height, GS_RGBA32F, 1, nullptr, GS_DYNAMIC);
		target_ = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
		stage_ = gs_stagesurface_create(grid.width, grid.height, GS_RGBA32F);
	}
	~Chroma()
	{
		Graphics lock;
		gs_stagesurface_destroy(stage_);
		gs_texrender_destroy(target_);
		gs_texture_destroy(input_);
		gs_effect_destroy(effect_);
	}
	Tensor run(const Tensor &rgb, const Settings &settings)
	{
		ck2::check_tensor(rgb, input_contract(grid_, true).at("rgb"), true);
		if (!effect_ || !input_ || !target_ || !stage_)
			throw std::runtime_error("Chroma hint shader is unavailable");
		const auto count = grid_.width * grid_.height * 1ULL;
		for (std::size_t p = 0; p < count; ++p) {
			for (std::size_t c = 0; c < 3; ++c)
				pixels_[4 * p + c] = rgb.values[c * count + p];
			pixels_[4 * p + 3] = 1;
		}
		Tensor result({1, 1, grid_.height, grid_.width});
		Graphics lock;
		Scene scene;
		gs_texture_set_image(input_, reinterpret_cast<const uint8_t *>(pixels_.data()), grid_.width * 16,
				     false);
		gs_texrender_reset(target_);
		if (!gs_texrender_begin(target_, grid_.width, grid_.height))
			throw std::runtime_error("Chroma hint render target failed");
		const bool srgb = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(false);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		gs_ortho(0, static_cast<float>(grid_.width), 0, static_cast<float>(grid_.height), -100, 100);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect_, "image"), input_);
		gs_effect_set_float(gs_effect_get_param_by_name(effect_, "similarity"), settings.hint.similarity);
		gs_effect_set_float(gs_effect_get_param_by_name(effect_, "smoothness"), settings.hint.smoothness);
		gs_effect_set_int(gs_effect_get_param_by_name(effect_, "blue_screen"),
				  settings.quality.screen_color == ScreenColor::blue ? 1 : 0);
		while (gs_effect_loop(effect_, "Draw"))
			gs_draw_sprite(input_, 0, grid_.width, grid_.height);
		gs_blend_state_pop();
		gs_enable_framebuffer_srgb(srgb);
		gs_texrender_end(target_);
		gs_stage_texture(stage_, gs_texrender_get_texture(target_));
		gs_flush();
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (!gs_stagesurface_map(stage_, &mapped, &stride))
			throw std::runtime_error("Chroma hint readback failed");
		for (unsigned y = 0; y < grid_.height; ++y)
			for (unsigned x = 0; x < grid_.width; ++x)
				std::memcpy(&result.values[y * grid_.width + x], mapped + y * stride + x * 16,
					    sizeof(float));
		gs_stagesurface_unmap(stage_);
		return result;
	}

private:
	Extent grid_;
	std::vector<float> pixels_;
	gs_effect_t *effect_ = nullptr;
	gs_texture_t *input_ = nullptr;
	gs_texrender_t *target_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
};
} // namespace
ChromaHint obs_chroma_hint(Extent grid, const char *path)
{
	auto shader = std::make_shared<Chroma>(grid, path);
	return [shader](const Tensor &rgb, const Settings &settings) {
		return shader->run(rgb, settings);
	};
}
} // namespace corridorkey::ck1
