// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "pipeline.hpp"
#include <cmath>
#include <limits>

namespace corridorkey {
Error error(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::none:
		return {};
	case ErrorCode::busy:
		return {code, "Wait for a free frame slot."};
	case ErrorCode::cancelled:
		return {code, "The task was cancelled."};
	case ErrorCode::stale_generation:
		return {code, "Discard the previous generation."};
	case ErrorCode::invalid_input:
		return {code, "Check the input frame and format."};
	case ErrorCode::invalid_output:
		return {code, "Discard the output and reset model state."};
	case ErrorCode::out_of_memory:
		return {code, "Try a smaller profile or close another model instance."};
	case ErrorCode::backend_unavailable:
		return {code, "Choose a backend that passes the capability test."};
	case ErrorCode::device_lost:
		return {code, "Retry the backend after the device recovers."};
	case ErrorCode::timeout:
		return {code, "Retry the backend test."};
	case ErrorCode::model_missing:
		return {code, "Open Models to install this model."};
	case ErrorCode::licence_required:
		return {code, "Review and accept the model licences."};
	case ErrorCode::hash_mismatch:
		return {code, "Download the artifact again."};
	case ErrorCode::disk_full:
		return {code, "Free space in the model cache location."};
	case ErrorCode::io_failure:
		return {code, "Check the connection and cache permissions."};
	case ErrorCode::unsupported:
		return {code, "Use a supported input or capability."};
	default:
		return {ErrorCode::internal, "Open diagnostics for the failed operation."};
	}
}
std::string_view error_id(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::none:
		return "CK-OK";
	case ErrorCode::busy:
		return "CK-E001";
	case ErrorCode::cancelled:
		return "CK-E002";
	case ErrorCode::stale_generation:
		return "CK-E003";
	case ErrorCode::invalid_input:
		return "CK-E004";
	case ErrorCode::invalid_output:
		return "CK-E005";
	case ErrorCode::out_of_memory:
		return "CK-E006";
	case ErrorCode::backend_unavailable:
		return "CK-E007";
	case ErrorCode::device_lost:
		return "CK-E008";
	case ErrorCode::timeout:
		return "CK-E009";
	case ErrorCode::model_missing:
		return "CK-E010";
	case ErrorCode::licence_required:
		return "CK-E011";
	case ErrorCode::hash_mismatch:
		return "CK-E012";
	case ErrorCode::disk_full:
		return "CK-E013";
	case ErrorCode::io_failure:
		return "CK-E014";
	case ErrorCode::unsupported:
		return "CK-E015";
	default:
		return "CK-E016";
	}
}
namespace {
bool valid_crop(const Crop &crop) noexcept
{
	return crop.left >= 0 && crop.top >= 0 && crop.right <= 1 && crop.bottom <= 1 && crop.left < crop.right &&
	       crop.top < crop.bottom;
}
bool valid_image(const Image &image, std::size_t channels, bool numeric) noexcept
{
	if (!image.storage || !image.size.width || !image.size.height)
		return false;
	if (image.storage->domain() != MemoryDomain::host)
		return image.storage->native_handle() != 0;
	const auto row = static_cast<std::uint64_t>(image.size.width) * channels * sizeof(float);
	if (row > std::numeric_limits<std::size_t>::max() || image.row_stride_bytes != row)
		return false;
	const auto pixels = static_cast<std::uint64_t>(image.size.width) * image.size.height;
	if (pixels > std::numeric_limits<std::size_t>::max() / channels)
		return false;
	const auto count = pixels * channels;
	auto values = image.storage->host_floats();
	if (count != values.size())
		return false;
	if (numeric)
		for (float value : values)
			if (!std::isfinite(value) || value < 0 || value > 1)
				return false;
	return true;
}
} // namespace
Error validate_input(const InputFrame &input) noexcept
{
	if (!input.lease || !input.settings || !input.identity.instance || !input.identity.sequence)
		return error(ErrorCode::invalid_input);
	if (input.identity.generation != input.lease.generation() || !input.lease.current())
		return error(ErrorCode::stale_generation);
	if (input.lease.state() != FrameState::ready || input.original.size != input.transform.source_size ||
	    !input.transform.source_size.width || !input.transform.source_size.height || !input.original.storage ||
	    !valid_crop(input.transform.user_crop) || !valid_crop(input.transform.sampled_roi))
		return error(ErrorCode::invalid_input);
	if (input.analysis.color == ColorSpace::hdr_unsupported || input.original.color == ColorSpace::hdr_unsupported)
		return error(ErrorCode::unsupported);
	return {};
}
Error validate_result(const FrameResult &result) noexcept
{
	if (result.failure)
		return result.failure;
	if (result.identity != result.input.identity || result.transform != result.input.transform ||
	    !result.input.lease.current())
		return error(ErrorCode::stale_generation);
	if (result.foreground.size != result.alpha.size || result.alpha.size != result.input.analysis.size ||
	    result.foreground.format != PixelFormat::rgb_f32 || result.alpha.format != PixelFormat::alpha_f32 ||
	    result.foreground.color != ColorSpace::srgb || result.foreground.alpha != AlphaMode::straight ||
	    result.alpha.color != ColorSpace::linear_srgb || !valid_image(result.foreground, 3, true) ||
	    !valid_image(result.alpha, 1, true))
		return error(ErrorCode::invalid_output);
	return {};
}
bool presentable(const FrameResult &result, InstanceId instance, Generation generation, Sequence last_presented,
		 TimePoint now, Duration maximum_age) noexcept
{
	return !result.failure && result.identity == result.input.identity &&
	       result.transform == result.input.transform && result.input.lease.current() &&
	       result.identity.instance == instance && result.identity.generation == generation &&
	       result.identity.sequence >= last_presented && now >= result.identity.captured_at &&
	       now - result.identity.captured_at <= maximum_age &&
	       (!result.output_complete || result.output_complete->ready());
}
} // namespace corridorkey
