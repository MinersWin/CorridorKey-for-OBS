// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "staged_transport.hpp"
#include <obs-module.h>
#include <algorithm>
#include <cstring>

namespace corridorkey {
namespace {
class TextureView final : public ImageStorage {
public:
	explicit TextureView(gs_texture_t *texture)
		: texture_(texture),
		  domain_(gs_get_device_type() == GS_DEVICE_OPENGL ? MemoryDomain::opengl : MemoryDomain::d3d11)
	{
	}
	MemoryDomain domain() const noexcept override { return domain_; }
	std::uintptr_t native_handle() const noexcept override { return reinterpret_cast<std::uintptr_t>(texture_); }

private:
	gs_texture_t *texture_;
	MemoryDomain domain_;
};
// Only queried with graphics ownership, just like FrameTransport::try_present.
class ReadCompletion final : public Completion {
public:
	explicit ReadCompletion(const GpuFence &fence) : fence_(fence) {}
	bool ready() const noexcept override { return fence_.ready(); }

private:
	const GpuFence &fence_;
};
bool prime(gs_texrender_t *target, Extent size)
{
	if (!target || !gs_texrender_begin(target, size.width, size.height))
		return false;
	gs_texrender_end(target);
	gs_texrender_reset(target);
	return gs_texrender_get_texture(target) != nullptr;
}
void set_crop(gs_eparam_t *parameter, const Crop &crop)
{
	vec4 bounds;
	vec4_set(&bounds, crop.left, crop.top, crop.right, crop.bottom);
	gs_effect_set_vec4(parameter, &bounds);
}
} // namespace
struct StagedTransport::Slot {
	gs_texrender_t *native = nullptr, *grid = nullptr;
	gs_stagesurf_t *stage = nullptr;
	gs_texture_t *output = nullptr;
	GpuFence captured, drawn;
	std::shared_ptr<const ImageStorage> original;
	std::shared_ptr<const Completion> read_complete = std::make_shared<ReadCompletion>(drawn);
	std::optional<InputFrame> pending;
	std::vector<std::uint8_t> bytes;
	~Slot()
	{
		gs_texrender_destroy(native);
		gs_texrender_destroy(grid);
		gs_stagesurface_destroy(stage);
		gs_texture_destroy(output);
	}
};
StagedTransport::StagedTransport(obs_source_t *source, Extent native, Extent grid)
	: source_(source),
	  native_(native),
	  grid_(grid)
{
	char *path = obs_module_file("effects/capture.effect");
	if (path) {
		effect_ = gs_effect_create_from_file(path, nullptr);
		bfree(path);
	}
	if (!effect_)
		return;
	image_ = gs_effect_get_param_by_name(effect_, "image");
	original_ = gs_effect_get_param_by_name(effect_, "original_image");
	result_ = gs_effect_get_param_by_name(effect_, "result_image");
	crop_ = gs_effect_get_param_by_name(effect_, "crop_bounds");
	if (!image_ || !original_ || !result_ || !crop_)
		return;
	for (auto &entry : slots_) {
		entry = std::make_unique<Slot>();
		auto &slot = *entry;
		if (!slot.captured.supported() || !slot.drawn.supported())
			return;
		slot.native = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		slot.grid = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		slot.stage = gs_stagesurface_create(grid.width, grid.height, GS_RGBA);
		slot.output = gs_texture_create(grid.width, grid.height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
		slot.bytes.resize(static_cast<std::size_t>(grid.width) * grid.height * 4);
		if (!prime(slot.native, native) || !prime(slot.grid, grid) || !slot.stage || !slot.output)
			return;
		slot.original = std::make_shared<TextureView>(gs_texrender_get_texture(slot.native));
	}
	available_ = true;
}
StagedTransport::~StagedTransport()
{
	for (auto &slot : slots_)
		slot.reset();
	gs_effect_destroy(effect_);
}
TransportCapabilities StagedTransport::capabilities() const
{
	TransportCapabilities result;
	result.kind = TransportKind::staged_grid;
	result.available = result.nonblocking_capture = result.native_rgb = available_;
	if (!available_)
		result.unavailable_reason = error(ErrorCode::unsupported);
	return result;
}
Error StagedTransport::try_capture(InputFrame &input) noexcept
{
	host_draw_time_ = {};
	if (!available_ || !input.lease || input.lease.id().index >= slots_.size())
		return error(ErrorCode::unsupported);
	auto &slot = *slots_[input.lease.id().index];
	if (slot.pending || !slot.captured.ready() || !slot.drawn.ready())
		return error(ErrorCode::busy);
	gs_texrender_reset(slot.native);
	if (!gs_texrender_begin(slot.native, native_.width, native_.height))
		return error(ErrorCode::device_lost);
	gs_ortho(0, static_cast<float>(native_.width), 0, static_cast<float>(native_.height), -100, 100);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	const auto before_host = Clock::now();
	const bool began = obs_source_process_filter_begin(source_, GS_RGBA, OBS_NO_DIRECT_RENDERING);
	host_draw_time_ = Clock::now() - before_host;
	if (began)
		obs_source_process_filter_tech_end(source_, effect_, native_.width, native_.height, "Capture");
	gs_blend_state_pop();
	gs_texrender_end(slot.native);
	if (!began)
		return error(ErrorCode::invalid_input);
	gs_texrender_reset(slot.grid);
	if (!gs_texrender_begin(slot.grid, grid_.width, grid_.height))
		return error(ErrorCode::device_lost);
	gs_ortho(0, static_cast<float>(grid_.width), 0, static_cast<float>(grid_.height), -100, 100);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_effect_set_texture(image_, gs_texrender_get_texture(slot.native));
	set_crop(crop_, input.transform.sampled_roi);
	while (gs_effect_loop(effect_, "Downscale"))
		gs_draw_sprite(gs_texrender_get_texture(slot.native), 0, grid_.width, grid_.height);
	gs_blend_state_pop();
	gs_texrender_end(slot.grid);
	gs_stage_texture(slot.stage, gs_texrender_get_texture(slot.grid));
	if (!slot.captured.signal()) {
		available_ = false;
		return error(ErrorCode::device_lost);
	}
	input.original = {native_,
			  PixelFormat::rgba8,
			  ColorSpace::srgb,
			  AlphaMode::premultiplied,
			  static_cast<std::size_t>(native_.width) * 4,
			  slot.original};
	slot.pending = std::move(input);
	return {};
}
bool StagedTransport::capture_ready(const InputFrame &input) noexcept
{
	const auto &slot = *slots_[input.lease.id().index];
	return input.identity.video_tick != obs_get_video_frame_time() && slot.captured.ready();
}
std::optional<InputFrame> StagedTransport::take_ready(std::uint64_t tick)
{
	for (auto &entry : slots_) {
		if (!entry || !entry->pending)
			continue;
		auto &slot = *entry;
		if (slot.pending->identity.video_tick == tick)
			continue;
		if (!slot.captured.ready()) {
			if (slot.captured.failed()) {
				failure_ = error(ErrorCode::device_lost);
				available_ = false;
				slot.pending.reset();
			}
			continue;
		}
		if (!slot.pending->lease.current()) {
			slot.pending.reset();
			continue;
		}
		std::uint8_t *data = nullptr;
		std::uint32_t pitch = 0;
		if (!gs_stagesurface_map(slot.stage, &data, &pitch)) {
			failure_ = error(ErrorCode::device_lost);
			available_ = false;
			slot.pending.reset();
			continue;
		}
		const auto row = static_cast<std::size_t>(grid_.width) * 4;
		const bool valid = data && pitch >= row;
		if (valid)
			for (std::uint32_t y = 0; y < grid_.height; ++y)
				std::memcpy(slot.bytes.data() + y * row, data + static_cast<std::size_t>(y) * pitch,
					    row);
		gs_stagesurface_unmap(slot.stage);
		if (!valid) {
			failure_ = error(ErrorCode::invalid_input);
			available_ = false;
			slot.pending.reset();
			continue;
		}
		auto input = std::move(slot.pending);
		slot.pending.reset();
		input->lease.transition(FrameState::ready);
		return input;
	}
	return std::nullopt;
}
void StagedTransport::discard_stale()
{
	for (auto &slot : slots_)
		if (slot && slot->pending && !slot->pending->lease.current() &&
		    (slot->captured.ready() || slot->captured.failed()))
			slot->pending.reset();
}
Image StagedTransport::analysis(const InputFrame &input) const
{
	const auto &bytes = slots_[input.lease.id().index]->bytes;
	std::vector<float> rgb(bytes.size() / 4 * 3);
	for (std::size_t p = 0; p < bytes.size() / 4; ++p)
		for (unsigned c = 0; c < 3; ++c)
			rgb[p * 3 + c] = static_cast<float>(bytes[p * 4 + c]) / 255.0f;
	return {grid_,
		PixelFormat::rgb_f32,
		ColorSpace::srgb,
		AlphaMode::opaque,
		static_cast<std::size_t>(grid_.width) * 3 * sizeof(float),
		std::make_shared<HostFloats>(std::move(rgb))};
}
bool StagedTransport::upload(const FrameResult &result, const std::vector<std::uint8_t> &rgba)
{
	if (rgba.size() != static_cast<std::size_t>(grid_.width) * grid_.height * 4)
		return false;
	gs_texture_set_image(slots_[result.input.lease.id().index]->output, rgba.data(), grid_.width * 4, false);
	return true;
}
Error StagedTransport::try_present(const FrameResult &result, std::shared_ptr<const Completion> &read_complete) noexcept
{
	auto &slot = *slots_[result.input.lease.id().index];
	gs_effect_set_texture(original_, gs_texrender_get_texture(slot.native));
	gs_effect_set_texture(result_, slot.output);
	set_crop(crop_, result.transform.sampled_roi);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	while (gs_effect_loop(effect_, "Composite"))
		gs_draw_sprite(slot.output, 0, native_.width, native_.height);
	gs_enable_framebuffer_srgb(srgb);
	gs_blend_state_pop();
	read_complete = slot.read_complete;
	if (!slot.drawn.signal()) {
		available_ = false;
		failure_ = error(ErrorCode::device_lost);
	}
	return failure_;
}
bool StagedTransport::read_finished(const FrameResult &result) const
{
	const auto &fence = slots_[result.input.lease.id().index]->drawn;
	return fence.ready() || fence.failed();
}
} // namespace corridorkey
