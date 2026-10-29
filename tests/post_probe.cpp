// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include <obs.h>
#include <graphics/vec4.h>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <mutex>

int run_post_tests(const char *directory);
int run_post_offline(const char *directory, const char *manifest);

namespace {
std::thread::id ui_thread;
std::mutex ui_mutex;
std::deque<std::function<void()>> ui_tasks;
void handle_ui(obs_task_t task, void *parameter, bool wait)
{
	if (std::this_thread::get_id() == ui_thread) {
		task(parameter);
		return;
	}
	auto completion = std::make_shared<std::promise<void>>();
	auto ready = completion->get_future();
	{
		std::lock_guard lock(ui_mutex);
		ui_tasks.emplace_back([=] {
			task(parameter);
			completion->set_value();
		});
	}
	if (wait)
		ready.wait();
}
void pump_ui()
{
	for (;;) {
		std::deque<std::function<void()>> tasks;
		{
			std::lock_guard lock(ui_mutex);
			tasks.swap(ui_tasks);
		}
		if (tasks.empty())
			return;
		for (auto &task : tasks)
			task();
	}
}
struct Source {
	std::atomic<unsigned> width, height;
	bool hdr = false;
};
const char *name(void *)
{
	return "Reconstruction probe";
}
void *create(obs_data_t *data, obs_source_t *)
{
	return new Source{static_cast<unsigned>(obs_data_get_int(data, "width")),
			  static_cast<unsigned>(obs_data_get_int(data, "height")), obs_data_get_bool(data, "hdr")};
}
void update(void *value, obs_data_t *data)
{
	auto &source = *static_cast<Source *>(value);
	source.width = static_cast<unsigned>(obs_data_get_int(data, "width"));
	source.height = static_cast<unsigned>(obs_data_get_int(data, "height"));
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
gs_color_space color_space(void *data, size_t, const gs_color_space *)
{
	return static_cast<Source *>(data)->hdr ? GS_CS_709_EXTENDED : GS_CS_SRGB;
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
		std::fprintf(stderr, "post_probe module data-directory width seconds [fake-delay-ms] [stress|hdr]\n");
		return 2;
	}
	if (!obs_startup("en-US", nullptr, nullptr))
		return 3;
	ui_thread = std::this_thread::get_id();
	obs_set_ui_task_handler(handle_ui);
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
	// OBS 33 deprecates obs_add_data_path, but this test host must also run against 30.2.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	if (const auto *data = std::getenv("OBS_TEST_DATA"))
		obs_add_data_path(data);
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
	if (std::strcmp(argv[4], "offline") == 0 && argc == 6) {
		const int result = run_post_offline(argv[2], argv[5]);
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
	info.update = update;
	info.get_width = width;
	info.get_height = height;
	info.video_render = render;
	info.video_get_color_space = color_space;
	obs_register_source(&info);
	auto *data = obs_data_create();
	obs_data_set_int(data, "width", w);
	obs_data_set_int(data, "height", w * 9 / 16);
	const bool hdr = argc > 6 && std::strcmp(argv[6], "hdr") == 0;
	obs_data_set_bool(data, "hdr", hdr);
	auto *source = obs_source_create(info.id, "Probe", data, nullptr);
	obs_data_release(data);
	data = obs_data_create();
	obs_data_set_bool(data, "w03_fake_enabled", true);
	obs_data_set_int(data, "w03_fake_delay", argc > 5 ? std::atoi(argv[5]) : 0);
	auto *filter = obs_source_create("corridorkey", "Probe key", data, nullptr);
	obs_data_release(data);
	obs_source_filter_add(source, filter);
	obs_set_output_source(0, source);
	const bool stress = argc > 6 && std::strcmp(argv[6], "stress") == 0;
	for (int half = 0; half < std::atoi(argv[4]) * 2; ++half) {
		for (int poll = 0; poll < 50; ++poll) {
			pump_ui();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!stress)
			continue;
		auto *change = obs_source_get_settings(source);
		obs_data_set_int(change, "width", half % 3 == 0 ? 719 : w);
		obs_data_set_int(change, "height", half % 3 == 0 ? 1281 : w * 9 / 16);
		obs_source_update(source, change);
		obs_data_release(change);
		change = obs_source_get_settings(filter);
		obs_data_set_double(change, "cut_left", half % 4 == 0 ? 17 : 0);
		obs_data_set_int(change, "w03_fake_delay", half % 3 == 0 ? 90 : 0);
		obs_data_set_bool(change, "w03_fake_reorder", half % 5 == 0);
		obs_data_set_int(change, "w03_fake_fault", half % 7 == 0 ? 3 : 0);
		obs_source_update(filter, change);
		obs_source_set_enabled(filter, half % 6 != 0);
		obs_data_release(change);
	}
	if (stress)
		std::puts("STRESS resize/crop/enable/failure transitions completed");
	bool verified = true;
	if (hdr) {
		const gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED, GS_CS_709_SCRGB};
		verified = obs_source_get_color_space(filter, 4, spaces) == GS_CS_709_EXTENDED;
		auto *properties = obs_source_properties(filter);
		auto *values = obs_source_get_settings(filter);
		obs_properties_apply_settings(properties, values);
		const auto *status = obs_data_get_string(values, "status");
		std::printf("HDR status: %s\n", status);
		verified = verified && std::strstr(status, "HDR") != nullptr;
		obs_data_release(values);
		obs_properties_destroy(properties);
	}
	obs_set_output_source(0, nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_release(source);
	obs_wait_for_destroy_queue();
	pump_ui();
	obs_shutdown();
	return verified ? 0 : 7;
}
