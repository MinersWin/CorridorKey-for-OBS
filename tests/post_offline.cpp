// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "gpu/post/reconstruction.hpp"
#include "gpu/post/refiner.hpp"
#include "models/tensor_storage.hpp"
#include "util/utf8_path.hpp"
#include <obs.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace corridorkey;
using Json = nlohmann::json;
struct Texture {
	gs_texture_t *value = nullptr;
	explicit Texture(const std::string &path)
	{
		models::storage::File file(utf8_path(path));
		const auto info = models::storage::read_npy(file);
		if (info.dtype != "float32" || info.shape.size() != 3 || info.shape[2] != 4 || info.shape[0] == 0 ||
		    info.shape[1] == 0 || info.shape[0] > 8192 || info.shape[1] > 8192)
			throw std::runtime_error("Expected HWC RGBA float32 NPY, dimensions 1..8192");
		std::vector<float> data(info.shape[0] * info.shape[1] * 4);
		file.read(info.offset,
			  std::span<char>(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float)));
		const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
		value = gs_texture_create(static_cast<uint32_t>(info.shape[1]), static_cast<uint32_t>(info.shape[0]),
					  GS_RGBA32F, 1, &bytes, 0);
		if (!value)
			throw std::runtime_error("Texture upload failed");
	}
	~Texture() { gs_texture_destroy(value); }
	Extent extent() const { return {gs_texture_get_width(value), gs_texture_get_height(value)}; }
};
void dump(gs_texture_t *texture, const std::string &path)
{
	const auto w = gs_texture_get_width(texture), h = gs_texture_get_height(texture);
	const auto format = gs_texture_get_color_format(texture);
	auto *stage = gs_stagesurface_create(w, h, format);
	if (!stage)
		throw std::runtime_error("Staging allocation failed");
	gs_stage_texture(stage, texture);
	gs_flush();
	uint8_t *bytes = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(stage, &bytes, &pitch)) {
		gs_stagesurface_destroy(stage);
		throw std::runtime_error("Staging map failed");
	}
	std::ofstream output(utf8_path(path), std::ios::binary);
	for (uint32_t y = 0; y < h; ++y)
		output.write(reinterpret_cast<char *>(bytes + y * pitch), w * (format == GS_RGBA16F ? 8 : 16));
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	if (!output)
		throw std::runtime_error("Output write failed");
}
void frame(const Json &item, const char *directory)
{
	Texture native(item.at("native")), guide(item.at("guide")), model(item.at("model"));
	Settings settings;
	settings.quality.unmix_inner = item.value("unmix_inner", 1.0f);
	settings.quality.despill_strength = item.value("despill", 0.65f);
	settings.quality.alpha_contrast = item.value("contrast", 1.05f);
	Crop roi;
	if (item.contains("roi")) {
		const auto &r = item.at("roi");
		roi = {r.at(0), r.at(1), r.at(2), r.at(3)};
	}
	Reconstruction post(native.extent(), model.extent(), (std::string(directory) + "/effects/post.effect").c_str());
	const unsigned budget = item.value("refiner_budget", 0U);
	if (!post.available() || !post.prepare(native.value, guide.value, model.value, settings, roi, budget != 0))
		throw std::runtime_error("Reconstruction preparation failed");
	const auto prefix = item.at("output").get<std::string>();
	dump(post.raw(), prefix + ".raw.f32");
	dump(post.coefficients(), prefix + ".coef.f32");
	dump(post.delta(), prefix + ".delta.f32");
	std::unique_ptr<TileRefiner> refiner;
	gs_texture_t *refined = nullptr;
	if (budget) {
		refiner = std::make_unique<TileRefiner>(native.extent(), budget,
							(std::string(directory) + "/effects/refiner.effect").c_str());
		if (!refiner->available() || !refiner->begin(post, native.value, model.value, roi))
			throw std::runtime_error("Refiner GPU preparation failed");
		dump(refiner->atlas(), prefix + ".atlas.f32");
		dump(refiner->selection(), prefix + ".selection.f32");
		dump(refiner->scores(), prefix + ".scores.f32");
		dump(post.background(), prefix + ".background.f32");
		dump(post.band(), prefix + ".band.f32");
		const auto tensor = corridorkey::refiner::unpack_atlas(refiner->read_atlas(), budget);
		std::ofstream input_tensor(utf8_path(prefix + ".x.f32"), std::ios::binary);
		input_tensor.write(reinterpret_cast<const char *>(tensor.values.data()),
				   static_cast<std::streamsize>(tensor.values.size() * sizeof(float)));
		if (!input_tensor)
			throw std::runtime_error("Cannot write refiner input tensor");
		if (item.contains("refiner_centres")) {
			const auto size = corridorkey::refiner::output_atlas(budget);
			std::vector<float> data(static_cast<std::size_t>(size.width) * size.height * 4);
			std::ifstream file(utf8_path(item.at("refiner_centres").get<std::string>()), std::ios::binary);
			if (!file.read(reinterpret_cast<char *>(data.data()),
				       static_cast<std::streamsize>(data.size() * sizeof(float))))
				throw std::runtime_error("Missing refiner centre output");
			if (!refiner->blend(data))
				throw std::runtime_error("Refiner blend failed");
			refined = refiner->raw();
			dump(refined, prefix + ".raw.f32");
		}
	}
	for (auto format : {GS_RGBA32F, GS_RGBA16F}) {
		auto *output = gs_texrender_create(format, GS_ZS_NONE);
		if (!post.finish(output, native.value, settings, roi, 0, refined)) {
			gs_texrender_destroy(output);
			throw std::runtime_error("Reconstruction finish failed");
		}
		dump(gs_texrender_get_texture(output), prefix + (format == GS_RGBA32F ? ".rgba.f32" : ".rgba.f16"));
		gs_texrender_destroy(output);
	}
	std::cout << "Reconstructed " << prefix << '\n';
}
} // namespace
int run_post_offline(const char *directory, const char *manifest)
{
	int result = 0;
	obs_enter_graphics();
	gs_begin_scene();
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);
	try {
		std::ifstream input(corridorkey::utf8_path(manifest));
		const auto items = Json::parse(input);
		for (const auto &item : items)
			frame(item, directory);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	gs_end_scene();
	obs_leave_graphics();
	return result;
}
