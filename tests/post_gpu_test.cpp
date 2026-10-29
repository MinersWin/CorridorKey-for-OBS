// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "post_reference.hpp"
#include "gpu/post/reconstruction.hpp"
#include <obs.h>
#include <graphics/vec4.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {
gs_texture_t *upload(const post_reference::Image &image)
{
	std::vector<float> values;
	for (const auto &pixel : image.pixels)
		for (double value : pixel)
			values.push_back(static_cast<float>(value));
	const auto *data = reinterpret_cast<const std::uint8_t *>(values.data());
	return gs_texture_create(image.width, image.height, GS_RGBA32F, 1, &data, 0);
}
post_reference::Image download(gs_texture_t *texture)
{
	const auto w = gs_texture_get_width(texture), h = gs_texture_get_height(texture);
	post_reference::Image result(w, h);
	const bool half = gs_texture_get_color_format(texture) == GS_RGBA16F;
	auto *stage = gs_stagesurface_create(w, h, half ? GS_RGBA16F : GS_RGBA32F);
	gs_stage_texture(stage, texture);
	gs_flush();
	std::uint8_t *bytes = nullptr;
	std::uint32_t pitch = 0;
	if (!gs_stagesurface_map(stage, &bytes, &pitch))
		throw std::runtime_error("Probe readback failed");
	for (unsigned y = 0; y < h; ++y)
		for (unsigned x = 0; x < w; ++x) {
			float pixel[4];
			if (half) {
				std::uint16_t bits[4];
				std::memcpy(bits, bytes + y * pitch + x * sizeof(bits), sizeof(bits));
				for (unsigned c = 0; c < 4; ++c) {
					const unsigned exponent = (bits[c] >> 10) & 31, fraction = bits[c] & 1023;
					pixel[c] =
						exponent == 31
							? std::numeric_limits<float>::infinity()
							: std::ldexp(
								  static_cast<float>(fraction + (exponent ? 1024 : 0)),
								  exponent ? static_cast<int>(exponent) - 25 : -24);
					if (bits[c] & 0x8000)
						pixel[c] = -pixel[c];
				}
			} else {
				std::memcpy(pixel, bytes + y * pitch + x * sizeof(pixel), sizeof(pixel));
			}
			for (unsigned c = 0; c < 4; ++c)
				result.at(x, y)[c] = pixel[c];
		}
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	return result;
}
bool compare(const char *field, gs_texture_t *texture, const post_reference::Image &reference, double tolerance)
{
	auto actual = download(texture);
	double maximum = 0, sum = 0;
	bool finite = true;
	for (std::size_t i = 0; i < actual.pixels.size(); ++i)
		for (unsigned c = 0; c < 4; ++c) {
			const auto error = std::abs(actual.pixels[i][c] - reference.pixels[i][c]);
			finite = finite && std::isfinite(error);
			maximum = std::max(maximum, error);
			sum += error;
		}
	std::printf("%s max=%.9g mae=%.9g tolerance=%.9g %s\n", field, maximum, sum / (actual.pixels.size() * 4),
		    tolerance, finite && maximum <= tolerance ? "PASS" : "FAIL");
	return finite && maximum <= tolerance;
}
} // namespace
int run_post_tests(const char *directory)
{
	using namespace corridorkey;
	using post_reference::Image;
	bool success = true;
	obs_enter_graphics();
	gs_begin_scene();
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);
	for (unsigned fixture = 0; fixture < 17; ++fixture) {
		const unsigned w = fixture >= 9 && fixture <= 10   ? 512
				   : fixture == 1 || fixture == 11 ? 1
				   : fixture == 2                  ? 5
								   : 11;
		const unsigned h = fixture == 9 ? 256 : fixture == 10 ? 512 : fixture == 1 ? 1 : fixture == 2 ? 17 : 7;
		const unsigned nw = fixture == 2 ? 19 : 37, nh = fixture == 2 ? 53 : 25;
		Image native(nw, nh), guide(w, h), model(w, h);
		for (unsigned y = 0; y < h; ++y)
			for (unsigned x = 0; x < w; ++x) {
				const double u = static_cast<double>(x + 1) / (w + 1),
					     v = static_cast<double>(y + 1) / (h + 1);
				guide.at(x, y) = {u, v, (u + v) / 3, 1};
				model.at(x, y) = {u * 0.8, v * 0.6, (u + v) / 2.5, std::clamp(u * 1.4 - 0.2, 0.0, 1.0)};
				if (fixture == 3) {
					guide.at(x, y) = {0.8, 0.8, 0.8, 1};
					model.at(x, y) = {0.8, 0.8, 0.8, 0.7};
				}
				if (fixture == 4)
					model.at(x, y)[3] = 1e-7;
				if (fixture == 5)
					model.at(x, y)[3] = 1;
			}
		for (unsigned y = 0; y < nh; ++y)
			for (unsigned x = 0; x < nw; ++x) {
				const double u = (x + 0.5) / nw, v = (y + 0.5) / nh;
				const double alpha = x % 7 == 0 ? 0 : x % 5 == 0 ? 1e-8 : x % 3 == 0 ? 0.5 : 1;
				native.at(x, y) = {u * alpha, v * alpha, (u + v) / 3 * alpha, alpha};
			}
		if (fixture == 8) {
			guide.at(0, 0)[0] = std::numeric_limits<double>::infinity();
			model.at(1, 0)[3] = std::numeric_limits<double>::quiet_NaN();
			native.at(2, 0)[1] = std::numeric_limits<double>::infinity();
		}
		Settings settings;
		Crop roi;
		if (fixture == 4)
			settings.quality.alpha_contrast = 1;
		if (fixture == 6) {
			roi = {0.12f, 0.08f, 0.92f, 0.85f};
			settings.context.crop = {0.2f, 0.15f, 0.81f, 0.9f};
			settings.quality.clip_black = 0.1f;
			settings.quality.clip_white = 0.83f;
			settings.quality.unmix_inner = 0.2f;
		}
		if (fixture == 15) {
			settings.quality.model = ModelFamily::ck1;
			settings.quality.screen_color = ScreenColor::blue;
			for (auto &p : model.pixels)
				p = {0.2, 0.3, 0.9, 0.7};
		}
		if (fixture == 16) {
			settings.quality.model = ModelFamily::ck1;
			settings.context.output = OutputView::hint;
			settings.quality.alpha_contrast = 4;
			settings.quality.clip_black = .1f;
			// Keep crop edges off pixel centres when comparing float shaders with double arithmetic.
			settings.context.crop = {.11f, .11f, .89f, .89f};
			for (auto &p : model.pixels)
				p = {.2, .3, .4, .3};
		}
		if (fixture == 7)
			settings.quality.unmix_mode = UnmixMode::compositing_equation;
		Image analysis_guide = guide;
		if (fixture == 13) {
			analysis_guide = Image(w * 2, h * 2);
			for (unsigned y = 0; y < h * 2; ++y)
				for (unsigned x = 0; x < w * 2; ++x)
					analysis_guide.at(x, y) = {double((x * 17 + y * 7) % 31) / 31,
								   double((x * 3 + y * 11) % 23) / 23,
								   double((x * 13 + y * 5) % 19) / 19, 1};
			for (unsigned y = 0; y < h; ++y)
				for (unsigned x = 0; x < w; ++x)
					for (unsigned c = 0; c < 4; ++c)
						guide.at(x, y)[c] = (analysis_guide.at(x * 2, y * 2)[c] +
								     analysis_guide.at(x * 2 + 1, y * 2)[c] +
								     analysis_guide.at(x * 2, y * 2 + 1)[c] +
								     analysis_guide.at(x * 2 + 1, y * 2 + 1)[c]) /
								    4;
		}
		if (fixture == 14) {
			settings.quality.unmix_inner = 0;
			settings.quality.despill_strength = 0;
			for (auto &p : model.pixels)
				p = {0.1, 0.9, 0.3, 1};
			for (unsigned y = 0; y < nh; ++y)
				for (unsigned x = 0; x < nw; ++x)
					native.at(x, y) = {x % 2 ? 0.8 : 0.2, y % 2 ? 0.7 : 0.1, 0.4, 1};
		}
		Image network = model;
		if (fixture == 12) {
			network = Image(2, 2);
			network.pixels = {{0.1, 0.2, 0.3, 0},
					  {0.8, 0.4, 0.2, 0.8},
					  {0.3, 0.7, 0.1, 0.6},
					  {0.9, 0.8, 0.5, 1}};
			for (unsigned y = 0; y < h; ++y)
				for (unsigned x = 0; x < w; ++x) {
					const auto u = std::clamp((x + 0.5) * 2 / w - 0.5, 0.0, 1.0);
					const auto v = std::clamp((y + 0.5) * 2 / h - 0.5, 0.0, 1.0);
					for (unsigned c = 0; c < 4; ++c)
						model.at(x, y)[c] = std::lerp(
							std::lerp(network.at(0, 0)[c], network.at(1, 0)[c], u),
							std::lerp(network.at(0, 1)[c], network.at(1, 1)[c], u), v);
				}
		}
		const auto reference = post_reference::reconstruct(native, guide, model, settings, roi);
		auto *native_texture = upload(native), *guide_texture = upload(analysis_guide),
		     *model_texture = upload(network);
		auto *output = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
		std::printf("FIXTURE %u grid=%ux%u native=%ux%u\n", fixture, w, h, nw, nh);
		{
			Reconstruction post({nw, nh}, {w, h},
					    (std::string(directory) + "/effects/post.effect").c_str());
			if (!post.available() ||
			    !post.prepare(native_texture, guide_texture, model_texture, settings, roi) ||
			    !post.finish(output, native_texture, settings, roi)) {
				std::printf("FAIL initialization or draw\n");
				success = false;
			} else {
				success =
					compare("coefficients", post.coefficients(), reference.coefficients, 0.0001) &&
					success;
				success = compare("delta", post.delta(), reference.delta, 0.000002) && success;
				success = compare("background", post.background(), reference.background, 0.00001) &&
					  success;
				success = compare("band", post.band(), reference.band, 0.000002) && success;
				Image reduced(32, 32);
				for (unsigned y = 0; y < 32; ++y)
					for (unsigned x = 0; x < 32; ++x) {
						const unsigned first_x = x * w / 32, first_y = y * h / 32;
						const unsigned end_x = std::max(first_x + 1, (x + 1) * w / 32),
							       end_y = std::max(first_y + 1, (y + 1) * h / 32);
						unsigned count = 0;
						for (unsigned gy = first_y; gy < end_y; ++gy)
							for (unsigned gx = first_x; gx < end_x; ++gx) {
								for (unsigned c = 0; c < 3; ++c)
									reduced.at(x, y)[c] +=
										std::isfinite(guide.at(gx, gy)[c])
											? guide.at(gx, gy)[c]
											: 0;
								const auto a = model.at(gx, gy)[3];
								reduced.at(x, y)[3] = std::max(
									reduced.at(x, y)[3], std::isfinite(a) ? a : 0);
								++count;
							}
						for (unsigned c = 0; c < 3; ++c)
							reduced.at(x, y)[c] /= count;
					}
				success = compare("reduction", post.reduction(), reduced, 0.00001) && success;
				success = compare("raw", post.raw(), reference.raw, 0.0001) && success;
				success = compare("finished", gs_texrender_get_texture(output), reference.output,
						  0.0001) &&
					  success;
				post.finish(output, native_texture, settings, roi, 0, post.raw());
				success = compare("refined-seam", gs_texrender_get_texture(output), reference.output,
						  0.0001) &&
					  success;
				auto *half_output = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
				post.finish(half_output, native_texture, settings, roi);
				success = compare("half-storage", gs_texrender_get_texture(half_output),
						  reference.output, 0.0005) &&
					  success;
				gs_texrender_destroy(half_output);
				for (bool linear : {false, true}) {
					auto *composite = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
					gs_texrender_begin(composite, nw, nh);
					gs_ortho(0, static_cast<float>(nw), 0, static_cast<float>(nh), -100, 100);
					vec4 backdrop;
					vec4_set(&backdrop, 0.18f, 0.32f, 0.07f, 1);
					gs_clear(GS_CLEAR_COLOR, &backdrop, 0, 0);
					const bool previous = gs_set_linear_srgb(linear);
					gs_enable_blending(true);
					post.present(gs_texrender_get_texture(output));
					gs_set_linear_srgb(previous);
					gs_texrender_end(composite);
					Image expected = reference.output;
					for (auto &pixel : expected.pixels) {
						for (unsigned c = 0; c < 3; ++c) {
							double color = pixel[c];
							if (!linear)
								color = pixel[3] > 1e-6 ? post_reference::encode(
												  color / pixel[3]) *
												  pixel[3]
											: 0;
							pixel[c] = color + backdrop.ptr[c] * (1 - pixel[3]);
						}
						pixel[3] = 1;
					}
					success = compare(linear ? "linear-composite" : "srgb-composite",
							  gs_texrender_get_texture(composite), expected, 0.0001) &&
						  success;
					gs_texrender_destroy(composite);
				}
			}
		}
		gs_texrender_destroy(output);
		gs_texture_destroy(native_texture);
		gs_texture_destroy(guide_texture);
		gs_texture_destroy(model_texture);
	}
	gs_end_scene();
	obs_leave_graphics();
	return success ? 0 : 1;
}
