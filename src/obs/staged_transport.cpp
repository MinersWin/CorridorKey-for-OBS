// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "staged_transport.hpp"
#include "telemetry/obs_clock.hpp"
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
	gs_texture_t *model = nullptr, *model_half = nullptr;
	gs_texture_t *device_model = nullptr, *device_model_half = nullptr;
	gs_texture_t *shared_grid = nullptr;
	ID3D11Texture2D *grid_handle = nullptr, *model_handle = nullptr, *model_half_handle = nullptr;
	bool interop_capture = false, shared_capture = false;
	gs_texrender_t *output = nullptr;
	GpuFence captured, drawn;
	std::shared_ptr<const ImageStorage> original;
	std::shared_ptr<const ImageStorage> grid_view, model_view, model_half_view;
	std::shared_ptr<const Completion> read_complete = std::make_shared<ReadCompletion>(drawn);
	std::optional<InputFrame> pending;
	std::vector<std::uint8_t> bytes;
	~Slot()
	{
		gs_texrender_destroy(native);
		gs_texrender_destroy(grid);
		gs_stagesurface_destroy(stage);
		gs_texture_destroy(model);
		gs_texture_destroy(model_half);
		gs_texture_destroy(device_model);
		gs_texture_destroy(device_model_half);
		gs_texture_destroy(shared_grid);
		gs_texrender_destroy(output);
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
	path = obs_module_file("effects/post.effect");
	if (path) {
		reconstruction_ = std::make_unique<Reconstruction>(native, grid, path);
		reconstruction_half_ = std::make_unique<Reconstruction>(native, Extent{grid.width / 2, grid.height / 2},
									path, reconstruction_.get());
		bfree(path);
	}
	if (!reconstruction_ || !reconstruction_->available() || !reconstruction_half_ ||
	    !reconstruction_half_->available())
		return;
	for (auto &entry : slots_) {
		entry = std::make_unique<Slot>();
		auto &slot = *entry;
		if (!slot.captured.supported() || !slot.drawn.supported())
			return;
		slot.native = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
		slot.grid = gs_texrender_create(GS_RGBA_UNORM, GS_ZS_NONE);
		slot.stage = gs_stagesurface_create(grid.width, grid.height, GS_RGBA_UNORM);
		slot.model = gs_texture_create(grid.width, grid.height, GS_RGBA32F, 1, nullptr, GS_DYNAMIC);
		slot.model_half =
			gs_texture_create(grid.width / 2, grid.height / 2, GS_RGBA32F, 1, nullptr, GS_DYNAMIC);
		slot.output = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
		slot.bytes.resize(static_cast<std::size_t>(grid.width) * grid.height * 4);
		if (!prime(slot.native, native) || !prime(slot.grid, grid) || !slot.stage || !slot.model ||
		    !slot.model_half || !prime(slot.output, native))
			return;
		slot.original = std::make_shared<TextureView>(gs_texrender_get_texture(slot.native));
		slot.grid_view = std::make_shared<TextureView>(gs_texrender_get_texture(slot.grid));
		if (gs_get_device_type() == GS_DEVICE_DIRECT3D_11) {
			slot.shared_grid =
				gs_texture_create(grid.width, grid.height, GS_RGBA_UNORM, 1, nullptr, GS_SHARED_TEX);
			slot.device_model =
				gs_texture_create(grid.width, grid.height, GS_RGBA32F, 1, nullptr, GS_SHARED_TEX);
			slot.device_model_half = gs_texture_create(grid.width / 2, grid.height / 2, GS_RGBA32F, 1,
								   nullptr, GS_SHARED_TEX);
			if (slot.shared_grid)
				slot.grid_handle = static_cast<ID3D11Texture2D *>(gs_texture_get_obj(slot.shared_grid));
			if (slot.device_model && slot.device_model_half) {
				slot.model_handle =
					static_cast<ID3D11Texture2D *>(gs_texture_get_obj(slot.device_model));
				slot.model_half_handle =
					static_cast<ID3D11Texture2D *>(gs_texture_get_obj(slot.device_model_half));
				slot.model_view = std::make_shared<TextureView>(slot.device_model);
				slot.model_half_view = std::make_shared<TextureView>(slot.device_model_half);
			}
		}
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
	result.kind = transport_kind();
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
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	struct RestoreSrgb {
		bool previous;
		~RestoreSrgb() { gs_enable_framebuffer_srgb(previous); }
	} restore{previous_srgb};
	gs_enable_framebuffer_srgb(false);
	gs_texrender_reset(slot.native);
	if (!gs_texrender_begin(slot.native, native_.width, native_.height))
		return error(ErrorCode::device_lost);
	gs_ortho(0, static_cast<float>(native_.width), 0, static_cast<float>(native_.height), -100, 100);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	const auto before_host = obs_now();
	const bool began = obs_source_process_filter_begin_with_color_space(source_, GS_RGBA16F, GS_CS_SRGB_16F,
									    OBS_NO_DIRECT_RENDERING);
	host_draw_time_ = obs_now() - before_host;
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
	slot.shared_capture = slot.shared_grid && interop_requested_ && !interop_failed_;
	if (slot.shared_capture)
		gs_copy_texture(slot.shared_grid, gs_texrender_get_texture(slot.grid));
	slot.interop_capture = interop_.load();
	if (!slot.interop_capture)
		gs_stage_texture(slot.stage, gs_texrender_get_texture(slot.grid));
	if (!slot.captured.signal()) {
		available_ = false;
		return error(ErrorCode::device_lost);
	}
	if (slot.interop_capture && slot.captured.can_wait())
		gs_flush();
	input.original = {native_,
			  PixelFormat::rgba_f16,
			  ColorSpace::srgb,
			  AlphaMode::premultiplied,
			  static_cast<std::size_t>(native_.width) * 4 * sizeof(std::uint16_t),
			  slot.original};
	slot.pending = std::move(input);
	return {};
}
bool StagedTransport::capture_ready(const InputFrame &input) noexcept
{
	const auto &slot = *slots_[input.lease.id().index];
	return slot.captured.ready();
}
std::optional<InputFrame> StagedTransport::take_ready()
{
	Slot *selected = nullptr;
	for (auto &entry : slots_) {
		if (!entry || !entry->pending)
			continue;
		auto &slot = *entry;
		if (!(slot.interop_capture && slot.captured.can_wait()) && !slot.captured.ready()) {
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
		if (!selected || slot.pending->identity.sequence > selected->pending->identity.sequence) {
			if (selected) {
				selected->pending.reset();
				++discarded_;
			}
			selected = &slot;
		} else {
			slot.pending.reset();
			++discarded_;
		}
	}
	if (!selected)
		return std::nullopt;
	auto &slot = *selected;
	if (slot.interop_capture) {
		auto input = std::move(slot.pending);
		slot.pending.reset();
		input->lease.transition(FrameState::ready);
		return input;
	}
	std::uint8_t *data = nullptr;
	std::uint32_t pitch = 0;
	if (!gs_stagesurface_map(slot.stage, &data, &pitch)) {
		failure_ = error(ErrorCode::device_lost);
		available_ = false;
		slot.pending.reset();
		return std::nullopt;
	}
	const auto row = static_cast<std::size_t>(grid_.width) * 4;
	const bool valid = data && pitch >= row;
	if (valid)
		for (std::uint32_t y = 0; y < grid_.height; ++y)
			std::memcpy(slot.bytes.data() + y * row, data + static_cast<std::size_t>(y) * pitch, row);
	gs_stagesurface_unmap(slot.stage);
	if (!valid) {
		failure_ = error(ErrorCode::invalid_input);
		available_ = false;
		slot.pending.reset();
		return std::nullopt;
	}
	auto input = std::move(slot.pending);
	slot.pending.reset();
	input->lease.transition(FrameState::ready);
	return input;
}

void StagedTransport::discard_stale()
{
	for (auto &slot : slots_)
		if (slot && slot->pending && !slot->pending->lease.current() &&
		    (slot->captured.ready() || slot->captured.failed()))
			slot->pending.reset();
}
Image StagedTransport::analysis(const InputFrame &input, nvidia::TextureTransfers *transfers,
				std::shared_ptr<nvidia::Context> context, dml::TextureTransfers *dml_transfers,
				std::shared_ptr<dml::Context> dml_context)
{
	if (!context && !dml_context)
		interop_ = false;
	interop_requested_ = bool(context) || bool(dml_context);
	auto &slot = *slots_[input.lease.id().index];
	if (dml_context && dml_transfers && slot.shared_capture && slot.grid_handle && slot.model_handle &&
	    slot.model_half_handle && !interop_failed_) {
		try {
			if (slot.interop_capture && slot.captured.can_wait() && !slot.captured.wait_ready())
				throw std::runtime_error("DML capture completion timed out");
			auto &texture = dml_transfers->get(dml_context, slot.grid_handle);
			ck2::Tensor tensor({1, 3, grid_.height, grid_.width}, 0, false);
			tensor.directml =
				texture.conversion_buffer(static_cast<std::size_t>(grid_.width) * grid_.height * 3 * 4);
			auto rgba = texture.read();
			const unsigned params[]{grid_.width, grid_.height, 0, 0, 0, texture.pitch(), texture.format()};
			dml_context->compute("rgba_to_planar", grid_.width * grid_.height * 3, *rgba, nullptr,
					     *tensor.directml, params);
			interop_ = true;
			return ck2::packed_image(tensor, PixelFormat::rgb_f32);
		} catch (const std::exception &e) {
			interop_ = false;
			interop_failed_ = true;
			blog(LOG_WARNING, "[corridorkey:w11] Input sharing failed; staged fallback: %s", e.what());
			if (slot.interop_capture)
				throw;
		}
	}
	if (context && transfers && slot.shared_capture && slot.grid_handle && slot.model_handle &&
	    slot.model_half_handle && !interop_failed_) {
		try {
			if (slot.interop_capture && slot.captured.can_wait() && !slot.captured.wait_ready())
				throw std::runtime_error("Shared capture completion timed out");
			auto &texture = transfers->get(context, slot.grid_handle);
			auto tensor = ck2::device_rgb(texture.read(), grid_, texture.format(),
						      texture.conversion_buffer(static_cast<std::size_t>(grid_.width) *
										grid_.height * 3 * sizeof(float)));
			context->synchronize();
			interop_ = true;
			return ck2::packed_image(tensor, PixelFormat::rgb_f32);
		} catch (const std::exception &e) {
			interop_ = false;
			interop_failed_ = true;
			blog(LOG_WARNING, "[corridorkey:w10] Input interop unavailable; using staged capture: %s",
			     e.what());
			if (slot.interop_capture)
				throw;
		}
	}
	if (slot.interop_capture)
		throw std::runtime_error("Discard interop capture after backend change");
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
void StagedTransport::release_analysis(FrameResult &result)
{
	// Release conversion allocations on the coordinator worker before graphics publication.
	result.input.analysis.storage = slots_[result.input.lease.id().index]->grid_view;
}
bool StagedTransport::upload_device(FrameResult &result, nvidia::TextureTransfers &transfers,
				    dml::TextureTransfers *dml_transfers)
{
	const auto alpha = std::dynamic_pointer_cast<const ck2::DeviceImage>(result.alpha.storage);
	const auto rgb = std::dynamic_pointer_cast<const ck2::DeviceImage>(result.foreground.storage);
	if (!alpha || !rgb || interop_failed_) {
		if (interop_) {
			interop_ = false;
			interop_failed_ = true;
		}
		return false;
	}
	auto &slot = *slots_[result.input.lease.id().index];
	auto *handle = result.alpha.size == grid_ ? slot.model_handle : slot.model_half_handle;
	if (!handle)
		return false;
	try {
		if (alpha->tensor.directml && rgb->tensor.directml && dml_transfers) {
			auto context = alpha->tensor.directml->context();
			auto &texture = dml_transfers->get(context, handle);
			const auto size = result.alpha.size;
			if (texture.pitch() != size.width * 16)
				throw std::runtime_error("DML output pitch mismatch");
			auto rgba = texture.conversion_buffer(static_cast<std::size_t>(size.width) * size.height * 16);
			const unsigned params[]{size.width, size.height, 0, 0, 0, 0, 0};
			context->compute("planar_to_rgba", size.width * size.height * 4, *rgb->tensor.directml,
					 alpha->tensor.directml.get(), *rgba, params);
			texture.write(*rgba);
			result.alpha.storage = result.foreground.storage =
				result.alpha.size == grid_ ? slot.model_view : slot.model_half_view;
			return true;
		}
		if (!alpha->tensor.device || !rgb->tensor.device)
			return false;
		auto context = alpha->tensor.device->context();
		unsigned pixels = result.alpha.size.width * result.alpha.size.height;
		auto &texture = transfers.get(context, handle);
		auto rgba = texture.conversion_buffer(static_cast<std::size_t>(pixels) * 4 * sizeof(float));
		auto a = alpha->tensor.device->pointer(), f = rgb->tensor.device->pointer(), dst = rgba->pointer();
		void *args[]{&f, &a, &dst, &pixels};
		context->launch("planar_to_rgba", pixels * 4, args);
		context->synchronize();
		texture.write(*rgba);
		result.alpha.storage = result.foreground.storage = result.alpha.size == grid_ ? slot.model_view
											      : slot.model_half_view;
		return true;
	} catch (const std::exception &e) {
		interop_ = false;
		interop_failed_ = true;
		blog(LOG_WARNING, "[corridorkey:w10] Output interop unavailable; using staged upload: %s", e.what());
		return false;
	}
}
bool StagedTransport::upload(const FrameResult &result, const std::vector<float> &rgba, int debug_view,
			     std::shared_ptr<refiner::Stage> refinement)
{
	const auto extent = result.alpha.size;
	const Extent half{grid_.width / 2, grid_.height / 2};
	if ((extent != grid_ && extent != half) || result.foreground.size != extent ||
	    (!rgba.empty() && rgba.size() != static_cast<std::size_t>(extent.width) * extent.height * 4))
		return false;
	auto &slot = *slots_[result.input.lease.id().index];
	auto *model = rgba.empty() ? (extent == grid_ ? slot.device_model : slot.device_model_half)
				   : (extent == grid_ ? slot.model : slot.model_half);
	if (!model)
		return false;
	auto &post = extent == grid_ ? *reconstruction_ : *reconstruction_half_;
	struct ProtectReads {
		GpuFence &fence;
		bool &available;
		Error &failure;
		~ProtectReads()
		{
			if (!fence.signal()) {
				available = false;
				failure = error(ErrorCode::device_lost);
			}
		}
	} protect{slot.drawn, available_, failure_};
	if (refine_phase_) {
		if (refine_sequence_ != result.identity.sequence)
			return false;
		if (refine_phase_ == 1) {
			if (tiles_->failed()) {
				refine_phase_ = 0;
				return false;
			}
			if (!tiles_->read_ready())
				return false;
			refine_gather_ms_ =
				std::chrono::duration<double, std::milli>(obs_now() - refine_started_).count();
			if (tiles_->interop()) {
				if (!refine_stage_->submit_device(
					    static_cast<ID3D11Texture2D *>(gs_texture_get_obj(tiles_->atlas())),
					    static_cast<ID3D11Texture2D *>(gs_texture_get_obj(tiles_->centres()))))
					return false;
				refine_phase_ = 3;
			} else {
				const auto read_start = obs_now();
				atlas_pixels_ = tiles_->read_atlas();
				refine_read_ms_ =
					std::chrono::duration<double, std::milli>(obs_now() - read_start).count();
				refine_phase_ = 2;
			}
		}
		if (refine_phase_ == 2) {
			if (!refine_stage_->submit(atlas_pixels_))
				return false;
			refine_phase_ = 3;
		}
		auto refined = refine_stage_->poll();
		if (!refined)
			return false;
		refine_phase_ = 0;
		if (!refined->failure.empty()) {
			if (tiles_->interop())
				refiner_interop_failed_ = true;
			blog(LOG_WARNING, "[corridorkey:w13] Refiner inference failed: %s", refined->failure.c_str());
			return false;
		}
		const bool finished = (refined->device ? tiles_->blend_device() : tiles_->blend(refined->rgba)) &&
				      post.finish(slot.output, gs_texrender_get_texture(slot.native),
						  *result.input.settings, result.transform.sampled_roi, debug_view,
						  tiles_->raw());
		if (refine_frames_++ % 60 == 0)
			blog(LOG_INFO,
			     "[corridorkey:w13] instance=%llu tiles=%u inference_ms=%.3f cuda_interval_ms=%.3f gather_wait_ms=%.3f read_ms=%.3f unpack_ms=%.3f pack_ms=%.3f total_ms=%.3f readback_bytes=%llu upload_bytes=%llu",
			     static_cast<unsigned long long>(result.identity.instance), refine_budget_,
			     refined->inference_ms, refined->cuda_interval_ms, refine_gather_ms_ + refined->wait_ms,
			     refined->device ? 0.0 : refine_read_ms_, refined->unpack_ms, refined->pack_ms,
			     std::chrono::duration<double, std::milli>(obs_now() - refine_started_).count(),
			     static_cast<unsigned long long>(refined->device ? 0 : tiles_->readback_bytes()),
			     static_cast<unsigned long long>(refined->rgba.size() * sizeof(float)));
		refine_stage_.reset();
		if (!slot.drawn.signal()) {
			available_ = false;
			failure_ = error(ErrorCode::device_lost);
		}
		return finished && available_;
	}
	if (!rgba.empty())
		gs_texture_set_image(model, reinterpret_cast<const std::uint8_t *>(rgba.data()),
				     extent.width * 4 * sizeof(float), false);
	if (refinement && result.input.settings && result.input.settings->quality.refiner_enabled) {
		const bool device_refiner = refinement->supports_device() && !refiner_interop_failed_ &&
					    gs_get_device_type() == GS_DEVICE_DIRECT3D_11;
		if (!tiles_ || refine_budget_ != refinement->budget() || tiles_->interop() != device_refiner) {
			char *path = obs_module_file("effects/refiner.effect");
			if (!path)
				return false;
			tiles_ = std::make_unique<TileRefiner>(native_, refinement->budget(), path, device_refiner);
			if (device_refiner && !tiles_->available()) {
				refiner_interop_failed_ = true;
				tiles_ = std::make_unique<TileRefiner>(native_, refinement->budget(), path, false);
			}
			bfree(path);
			refine_budget_ = refinement->budget();
		}
		refine_started_ = obs_now();
		if (!tiles_->available() ||
		    !post.prepare(gs_texrender_get_texture(slot.native), gs_texrender_get_texture(slot.grid), model,
				  *result.input.settings, result.transform.sampled_roi, true) ||
		    !tiles_->begin(post, gs_texrender_get_texture(slot.native), model, result.transform.sampled_roi))
			return false;
		refine_stage_ = std::move(refinement);
		refine_sequence_ = result.identity.sequence;
		refine_phase_ = 1;
		if (!slot.drawn.signal()) {
			available_ = false;
			failure_ = error(ErrorCode::device_lost);
			refine_phase_ = 0;
		}
		if (refine_phase_ && tiles_->interop() && tiles_->can_wait()) {
			gs_flush();
			if (refine_stage_->submit_device(
				    static_cast<ID3D11Texture2D *>(gs_texture_get_obj(tiles_->atlas())),
				    static_cast<ID3D11Texture2D *>(gs_texture_get_obj(tiles_->centres())),
				    [tiles = tiles_.get()] { return tiles->wait_ready(); })) {
				refine_gather_ms_ =
					std::chrono::duration<double, std::milli>(obs_now() - refine_started_).count();
				refine_phase_ = 3;
			}
		}
		return false;
	}
	const bool finished = result.input.settings &&
			      post.prepare(gs_texrender_get_texture(slot.native), gs_texrender_get_texture(slot.grid),
					   model, *result.input.settings, result.transform.sampled_roi) &&
			      post.finish(slot.output, gs_texrender_get_texture(slot.native), *result.input.settings,
					  result.transform.sampled_roi, debug_view);
	// Even a partially submitted post pass retains its native input until the
	// GPU is done; presentation will extend this fence to include the draw.
	if (!slot.drawn.signal()) {
		available_ = false;
		failure_ = error(ErrorCode::device_lost);
	}
	return finished && available_;
}
Error StagedTransport::try_present(const FrameResult &result, std::shared_ptr<const Completion> &read_complete) noexcept
{
	auto &slot = *slots_[result.input.lease.id().index];
	reconstruction_->present(gs_texrender_get_texture(slot.output));
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
