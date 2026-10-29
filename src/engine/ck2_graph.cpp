// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck2_graph.hpp"
#include "refiner_stage.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace corridorkey::ck2 {
Tensor::Tensor(std::vector<std::int64_t> dimensions, float fill, bool host_storage) : shape(std::move(dimensions))
{
	std::size_t count = 1;
	if (shape.empty() || shape.size() > 8)
		throw std::invalid_argument("Invalid tensor rank");
	for (auto d : shape) {
		if (d <= 0 || static_cast<std::uint64_t>(d) > 64 * 1024 * 1024 / count)
			throw std::invalid_argument("Invalid or excessive tensor dimensions");
		count *= static_cast<std::size_t>(d);
	}
	if (host_storage)
		values.assign(count, fill);
}
std::string graph_name(Stage stage, Extent grid)
{
	if (stage == Stage::refiner) {
		refiner::input_atlas(grid.width);
		return "refiner_b" + std::to_string(grid.width);
	}
	const char *name = stage == Stage::moge     ? "moge"
			   : stage == Stage::rvm    ? "rvm"
			   : stage == Stage::stage1 ? "stage1nomask"
						    : "stage2nomask";
	return std::string(name) + "_" + std::to_string(grid.height) + "x" + std::to_string(grid.width);
}
Tensors input_contract(Stage stage, Extent grid)
{
	if (stage == Stage::refiner) {
		refiner::input_atlas(grid.width);
		return {{"x", Tensor({grid.width, 11, 52, 52})}};
	}
	const std::int64_t h = grid.height, w = grid.width;
	if (w != 512 || (h != 256 && h != 512))
		throw std::invalid_argument("CK2 supports 256x512 and 512x512 exports");
	switch (stage) {
	case Stage::refiner:
		break;
	case Stage::moge:
		return {{"rgb_moge", Tensor({1, 3, h * 7 / 32, w * 7 / 32})}};
	case Stage::rvm:
		return {{"rgb_rvm", Tensor({1, 3, h / 4, w / 4})}, {"state", Tensor({1, 1 + h * w / 4096, 384})}};
	case Stage::stage1:
		return {{"rgb", Tensor({1, 3, h, w})},
			{"moge_feat", Tensor({1, 1, 768, h / 64, w / 64})},
			{"rvm_feat", Tensor({1, 1, 384, h / 64, w / 64})},
			{"hint", Tensor({1, 1, 1, h, w})},
			{"hint_quality", Tensor({1, 1}, -1)}};
	case Stage::stage2:
		return {{"rgb", Tensor({1, 3, h, w})}, {"baton", Tensor({1, 1, 512, h / 16, w / 16})}};
	}
	throw std::invalid_argument("Unknown CK2 stage");
}
Tensors output_contract(Stage stage, Extent grid)
{
	const std::int64_t h = grid.height, w = grid.width;
	switch (stage) {
	case Stage::refiner:
		refiner::input_atlas(grid.width);
		return {{"alpha", Tensor({grid.width, 1, 52, 52})}, {"fg", Tensor({grid.width, 3, 52, 52})}};
	case Stage::moge:
		return {{"moge_feat", Tensor({1, 1, 768, h / 64, w / 64})}};
	case Stage::rvm:
		return {{"rvm_feat", Tensor({1, 1, 384, h / 64, w / 64})},
			{"state_out", Tensor({1, 1 + h * w / 4096, 384})}};
	case Stage::stage1:
		return {{"baton", Tensor({1, 1, 512, h / 16, w / 16})},
			{"alpha", Tensor({1, 1, h / 2, w / 2})},
			{"fg", Tensor({1, 3, h / 2, w / 2})}};
	case Stage::stage2:
		return {{"alpha", Tensor({1, 1, h, w})}, {"fg", Tensor({1, 3, h, w})}};
	}
	throw std::invalid_argument("Unknown CK2 stage");
}
void check_tensor(const Tensor &actual, const Tensor &expected, bool unit_range)
{
	std::size_t count = 1;
	for (auto d : expected.shape) {
		if (d <= 0 || static_cast<std::uint64_t>(d) > 64 * 1024 * 1024 / count)
			throw std::runtime_error("Invalid tensor shape");
		count *= static_cast<std::size_t>(d);
	}
	if (actual.shape != expected.shape || (actual.directml ? actual.directml->bytes() / sizeof(float)
					       : actual.device ? actual.device->bytes() / sizeof(float)
							       : actual.values.size()) != count)
		throw std::runtime_error("CK2 tensor shape mismatch");
	if (actual.directml) {
		actual.directml->context()->validate(*actual.directml, unit_range);
		return;
	}
	if (actual.device) {
		actual.device->context()->validate(*actual.device, unit_range);
		return;
	}
	for (float v : actual.values)
		if ((std::bit_cast<std::uint32_t>(v) & 0x7f800000U) == 0x7f800000U || (unit_range && (v < 0 || v > 1)))
			throw std::runtime_error("CK2 tensor has invalid values");
}
Tensor planar_rgb(const Image &image)
{
	if (const auto storage = std::dynamic_pointer_cast<const DeviceImage>(image.storage))
		return storage->tensor;
	if (!image.storage || image.storage->domain() != MemoryDomain::host || image.color != ColorSpace::srgb ||
	    image.format != PixelFormat::rgb_f32 || image.row_stride_bytes != image.size.width * 3ULL * sizeof(float))
		throw std::invalid_argument("CK2 requires packed host sRGB input");
	Tensor result({1, 3, image.size.height, image.size.width});
	const auto rgb = image.storage->host_floats();
	if (rgb.size() != result.values.size())
		throw std::invalid_argument("CK2 input storage size mismatch");
	const auto pixels = rgb.size() / 3;
	for (std::size_t p = 0; p < pixels; ++p)
		for (std::size_t c = 0; c < 3; ++c) {
			const auto v = rgb[p * 3 + c];
			if (!std::isfinite(v) || v < 0 || v > 1)
				throw std::invalid_argument("CK2 input is outside sRGB [0,1]");
			result.values[c * pixels + p] = v;
		}
	return result;
}
namespace {
Tensor resize(const Tensor &input, Extent size, bool area)
{
	if (input.shape.size() != 4 || input.shape[0] != 1 || !size.width || !size.height)
		throw std::invalid_argument("Resize requires a single NCHW image");
	const auto h = static_cast<int>(input.shape[2]), w = static_cast<int>(input.shape[3]);
	Tensor result({1, input.shape[1], size.height, size.width});
	for (int c = 0; c < input.shape[1]; ++c)
		for (unsigned y = 0; y < size.height; ++y)
			for (unsigned x = 0; x < size.width; ++x) {
				float value = 0;
				auto at = [&](int yy, int xx) {
					return input
						.values[(static_cast<std::size_t>(c) * h + std::clamp(yy, 0, h - 1)) * w +
							std::clamp(xx, 0, w - 1)];
				};
				if (area) {
					const int y0 =
						static_cast<int>(static_cast<std::uint64_t>(y) * h / size.height);
					const int y1 = static_cast<int>(
						(static_cast<std::uint64_t>(y + 1) * h + size.height - 1) /
						size.height);
					const int x0 = static_cast<int>(static_cast<std::uint64_t>(x) * w / size.width);
					const int x1 = static_cast<int>(
						(static_cast<std::uint64_t>(x + 1) * w + size.width - 1) / size.width);
					for (int yy = y0; yy < y1; ++yy)
						for (int xx = x0; xx < x1; ++xx)
							value += at(yy, xx);
					value /= static_cast<float>((y1 - y0) * (x1 - x0));
				} else {
					const float sy = (static_cast<float>(y) + .5f) * static_cast<float>(h) /
								 static_cast<float>(size.height) -
							 .5f;
					const float sx = (static_cast<float>(x) + .5f) * static_cast<float>(w) /
								 static_cast<float>(size.width) -
							 .5f;
					const int y0 = static_cast<int>(std::floor(sy)),
						  x0 = static_cast<int>(std::floor(sx));
					const float dy = sy - static_cast<float>(y0), dx = sx - static_cast<float>(x0);
					value = std::lerp(std::lerp(at(y0, x0), at(y0, x0 + 1), dx),
							  std::lerp(at(y0 + 1, x0), at(y0 + 1, x0 + 1), dx), dy);
				}
				result.values[(static_cast<std::size_t>(c) * size.height + y) * size.width + x] = value;
			}
	return result;
}
} // namespace
Tensor resize_area(const Tensor &input, Extent size, std::shared_ptr<nvidia::Buffer> reuse,
		   std::shared_ptr<dml::Buffer> dml_reuse)
{
	if (input.directml) {
		if (input.shape.size() != 4 || input.shape[0] != 1 || !size.width || !size.height)
			throw std::invalid_argument("Invalid DML resize shape");
		Tensor result({1, input.shape[1], size.height, size.width}, 0, false);
		auto context = input.directml->context();
		const auto bytes = static_cast<std::size_t>(input.shape[1]) * size.width * size.height * sizeof(float);
		result.directml = dml_reuse ? std::move(dml_reuse) : context->allocate(bytes);
		if (result.directml->bytes() != bytes || result.directml == input.directml)
			throw std::invalid_argument("DML resize buffer mismatch");
		const unsigned params[]{static_cast<unsigned>(input.shape[3]),
					static_cast<unsigned>(input.shape[2]),
					size.width,
					size.height,
					static_cast<unsigned>(input.shape[1]),
					0,
					0};
		context->compute("resize_area", static_cast<unsigned>(bytes / 4), *input.directml, nullptr,
				 *result.directml, params);
		return result;
	}
	if (input.device) {
		if (input.shape.size() != 4 || input.shape[0] != 1 || !size.width || !size.height)
			throw std::invalid_argument("Resize requires a single NCHW image");
		Tensor result;
		unsigned channels = static_cast<unsigned>(input.shape[1]);
		unsigned h = static_cast<unsigned>(input.shape[2]), w = static_cast<unsigned>(input.shape[3]);
		result.shape = {1, channels, size.height, size.width};
		const auto context = input.device->context();
		const auto bytes = static_cast<std::size_t>(size.width) * size.height * channels * sizeof(float);
		if (reuse && (reuse->bytes() != bytes || reuse->context()->ordinal() != context->ordinal() ||
			      reuse == input.device))
			throw std::invalid_argument("Resize buffer mismatch");
		result.device = reuse ? std::move(reuse) : context->allocate(bytes);
		auto src = input.device->pointer(), dst = result.device->pointer();
		void *args[]{&src, &dst, &w, &h, &size.width, &size.height, &channels};
		context->launch("resize_area", size.width * size.height * channels, args);
		return result;
	}
	return resize(input, size, true);
}
Tensor resize_bilinear(const Tensor &input, Extent size)
{
	return resize((input.device || input.directml) ? download_tensor(input) : input, size, false);
}
Image packed_image(const Tensor &input, PixelFormat format)
{
	const auto channels = format == PixelFormat::rgb_f32 ? 3U : 1U;
	if (input.shape.size() != 4 || input.shape[0] != 1 || input.shape[1] != channels)
		throw std::invalid_argument("Invalid decoded image shape");
	const Extent size{static_cast<unsigned>(input.shape[3]), static_cast<unsigned>(input.shape[2])};
	const auto pixels = static_cast<std::size_t>(size.width) * size.height;
	if (input.device || input.directml)
		return {size,
			format,
			channels == 3 ? ColorSpace::srgb : ColorSpace::linear_srgb,
			AlphaMode::straight,
			size.width * channels * sizeof(float),
			std::make_shared<DeviceImage>(input)};
	std::vector<float> values(input.values.size());
	for (std::size_t p = 0; p < pixels; ++p)
		for (unsigned c = 0; c < channels; ++c)
			values[p * channels + c] = input.values[c * pixels + p];
	return {size,
		format,
		channels == 3 ? ColorSpace::srgb : ColorSpace::linear_srgb,
		AlphaMode::straight,
		size.width * channels * sizeof(float),
		std::make_shared<HostFloats>(std::move(values))};
}
Tensor download_tensor(const Tensor &tensor)
{
	if (tensor.directml) {
		Tensor result(tensor.shape);
		tensor.directml->download(result.values.data(), result.values.size() * sizeof(float));
		return result;
	}
	if (!tensor.device)
		return tensor;
	Tensor result(tensor.shape);
	tensor.device->download(result.values.data(), result.values.size() * sizeof(float));
	return result;
}
void zero_tensor(Tensor &tensor)
{
	if (tensor.directml)
		tensor.directml->zero();
	else if (tensor.device)
		tensor.device->zero();
	else
		std::fill(tensor.values.begin(), tensor.values.end(), 0.f);
}
Tensor device_rgb(const std::shared_ptr<nvidia::Buffer> &rgba, Extent size, unsigned format,
		  std::shared_ptr<nvidia::Buffer> reuse)
{
	Tensor result;
	result.shape = {1, 3, size.height, size.width};
	unsigned pixels = size.width * size.height;
	const auto bytes = static_cast<std::size_t>(pixels) * 3 * sizeof(float);
	if (reuse &&
	    (reuse->bytes() != bytes || reuse->context()->ordinal() != rgba->context()->ordinal() || reuse == rgba))
		throw std::invalid_argument("RGB conversion buffer mismatch");
	result.device = reuse ? std::move(reuse) : rgba->context()->allocate(bytes);
	auto src = rgba->pointer(), dst = result.device->pointer();
	void *args[]{&src, &dst, &pixels, &format};
	rgba->context()->launch("rgba_to_planar", pixels * 3, args);
	return result;
}
} // namespace corridorkey::ck2
