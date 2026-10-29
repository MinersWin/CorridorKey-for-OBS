// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include <obs.h>
#include <graphics/vec4.h>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cstring>

int run_post_tests(const char *directory);

namespace {
struct Source {
	unsigned width, height;
};
const char *name(void *)
{
	return "Reconstruction probe";
}
void *create(obs_data_t *data, obs_source_t *)
{
	return new Source{static_cast<unsigned>(obs_data_get_int(data, "width")),
			  static_cast<unsigned>(obs_data_get_int(data, "height"))};
}
void destroy(void *data)
{
	delete static_cast<Source *>(data);
}
uint32_t width(void *data)
{
	return static_cast<Source *>(data)->width;
}
uint32_t height(void *data)
{
	return static_cast<Source *>(data)->height;
}
void render(void *data, gs_effect_t *)
{
	auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	vec4 color;
	vec4_set(&color, 0.8f, 0.4f, 0.6f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
	while (gs_effect_loop(effect, "Solid"))
		gs_draw_sprite(nullptr, 0, width(data), height(data));
}
} // namespace
int main(int argc, char **argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "post_probe module data-directory width seconds\n");
		return 2;
	}
	if (!obs_startup("en-US", nullptr, nullptr))
		return 3;
	obs_audio_info audio{};
	audio.samples_per_sec = 48000;
	audio.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&audio))
		return 3;
	const auto w = static_cast<unsigned>(std::atoi(argv[3]));
	obs_video_info video{};
#ifdef _WIN32
	video.graphics_module = "libobs-d3d11.dll";
#else
	video.graphics_module = "libobs-opengl.so";
#endif
	if (const auto *graphics = std::getenv("OBS_TEST_GRAPHICS"))
		video.graphics_module = graphics;
	if (const auto *data = std::getenv("OBS_TEST_DATA"))
		obs_add_data_path(data);
	video.fps_num = 60;
	video.fps_den = 1;
	video.base_width = video.output_width = w;
	video.base_height = video.output_height = w * 9 / 16;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.colorspace = VIDEO_CS_709;
	video.range = VIDEO_RANGE_FULL;
	video.scale_type = OBS_SCALE_BILINEAR;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS)
		return 4;
	if (std::strcmp(argv[4], "numeric") == 0) {
		const int result = run_post_tests(argv[2]);
		obs_shutdown();
		return result;
	}
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module))
		return 5;
	obs_source_info info{};
	info.id = "post_probe_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = name;
	info.create = create;
	info.destroy = destroy;
	info.get_width = width;
	info.get_height = height;
	info.video_render = render;
	obs_register_source(&info);
	auto *data = obs_data_create();
	obs_data_set_int(data, "width", w);
	obs_data_set_int(data, "height", w * 9 / 16);
	auto *source = obs_source_create(info.id, "Probe", data, nullptr);
	obs_data_release(data);
	data = obs_data_create();
	obs_data_set_bool(data, "w03_fake_enabled", true);
	obs_data_set_int(data, "w03_fake_delay", 0);
	auto *filter = obs_source_create("corridorkey", "Probe key", data, nullptr);
	obs_data_release(data);
	obs_source_filter_add(source, filter);
	obs_set_output_source(0, source);
	std::this_thread::sleep_for(std::chrono::seconds(std::atoi(argv[4])));
	obs_set_output_source(0, nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_release(source);
	obs_wait_for_destroy_queue();
	obs_shutdown();
	return 0;
}
