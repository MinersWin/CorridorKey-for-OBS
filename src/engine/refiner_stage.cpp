// SPDX-License-Identifier: GPL-3.0-or-later
#include "refiner_stage.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace corridorkey::refiner {
namespace {
inline float half_input(float v)
{
	// Round the research input cast to binary16, retaining the graph's FP32 external type.
	auto bits = std::bit_cast<std::uint32_t>(v);
	if (bits >= 0x38800000U)
		return std::bit_cast<float>((bits + 0xfffU + ((bits >> 13) & 1U)) & 0xffffe000U);
	const auto scaled = v * 16777216.0f;
	auto mantissa = static_cast<unsigned>(scaled);
	const float fraction = scaled - float(mantissa);
	if (fraction > 0.5f || (fraction == 0.5f && (mantissa & 1U)))
		++mantissa;
	return float(mantissa) / 16777216.0f;
}
#if defined(__SSE2__) || defined(_M_X64)
inline __m128 half_inputs(__m128 values)
{
	const auto valid = _mm_and_ps(_mm_cmpge_ps(values, _mm_setzero_ps()), _mm_cmple_ps(values, _mm_set1_ps(1)));
	if (_mm_movemask_ps(valid) != 15)
		throw std::runtime_error("Invalid refiner input pixel");
	const auto bits = _mm_castps_si128(values);
	const auto small = _mm_and_si128(_mm_cmpgt_epi32(bits, _mm_setzero_si128()),
					 _mm_cmplt_epi32(bits, _mm_set1_epi32(0x38800000)));
	if (_mm_movemask_epi8(small)) {
		float scalar[4];
		_mm_storeu_ps(scalar, values);
		for (auto &v : scalar)
			v = half_input(v);
		return _mm_loadu_ps(scalar);
	}
	const auto even = _mm_and_si128(_mm_srli_epi32(bits, 13), _mm_set1_epi32(1));
	return _mm_castsi128_ps(
		_mm_and_si128(_mm_add_epi32(bits, _mm_add_epi32(_mm_set1_epi32(0xfff), even)), _mm_set1_epi32(-8192)));
}
#endif
} // namespace
bool supported_budget(unsigned n) noexcept
{
	return n == 256 || n == 384 || n == 512 || n == 768;
}
Extent input_atlas(unsigned n)
{
	if (!supported_budget(n))
		throw std::invalid_argument("Unsupported refiner tile budget");
	return {atlas_columns * patch, n / atlas_columns * patch * 3};
}
Extent output_atlas(unsigned n)
{
	input_atlas(n);
	return {atlas_columns * tile, n / atlas_columns * tile};
}
ck2::Tensor unpack_atlas(std::span<const float> rgba, unsigned n)
{
	const auto size = input_atlas(n);
	if (rgba.size() != static_cast<std::size_t>(size.width) * size.height * 4)
		throw std::invalid_argument("Refiner atlas size mismatch");
	ck2::Tensor x({n, channels, patch, patch});
#if defined(__SSE2__) || defined(_M_X64)
	// Transpose four RGBA pixels at a time into contiguous planar rows. SSE2 is
	// baseline on x64; small half values retain the scalar ties-to-even conversion.
	for (unsigned b = 0; b < n; ++b)
		for (unsigned group = 0; group < 3; ++group)
			for (unsigned y = 0; y < patch; ++y)
				for (unsigned px = 0; px < patch; px += 4) {
					const auto ay = group * (size.height / 3) + b / atlas_columns * patch + y;
					const auto ax = b % atlas_columns * patch + px;
					const auto *source = rgba.data() + (ay * size.width + ax) * 4;
					__m128 v[4] = {_mm_loadu_ps(source), _mm_loadu_ps(source + 4),
						       _mm_loadu_ps(source + 8), _mm_loadu_ps(source + 12)};
					_MM_TRANSPOSE4_PS(v[0], v[1], v[2], v[3]);
					for (unsigned c = 0; c < (group == 2 ? 3U : 4U); ++c)
						_mm_storeu_ps(x.values.data() +
								      ((b * channels + group * 4 + c) * patch + y) *
									      patch +
								      px,
							      half_inputs(v[c]));
				}
#else
	for (unsigned b = 0; b < n; ++b)
		for (unsigned c = 0; c < channels; ++c)
			for (unsigned y = 0; y < patch; ++y)
				for (unsigned px = 0; px < patch; ++px) {
					const auto ay = c / 4 * (size.height / 3) + b / atlas_columns * patch + y;
					const auto ax = b % atlas_columns * patch + px;
					const float v = rgba[(ay * size.width + ax) * 4 + c % 4];
					if (!(v >= 0 && v <= 1))
						throw std::runtime_error("Invalid refiner input pixel");
					x.values[((b * channels + c) * patch + y) * patch + px] = half_input(v);
				}
#endif
	return x;
}
std::vector<float> pack_centres(const ck2::Tensors &outputs, unsigned n)
{
	const auto size = output_atlas(n);
	const auto &a = outputs.at("alpha"), &f = outputs.at("fg");
	auto check = [n](const ck2::Tensor &tensor, unsigned count) {
		if (tensor.shape != std::vector<std::int64_t>{n, count, patch, patch} ||
		    tensor.values.size() != static_cast<std::size_t>(n) * count * patch * patch)
			throw std::runtime_error("Refiner output shape mismatch");
		for (float v : tensor.values)
			if (!(v >= 0 && v <= 1))
				throw std::runtime_error("Invalid refiner output pixel");
	};
	check(a, 1);
	check(f, 3);
	std::vector<float> rgba(static_cast<std::size_t>(size.width) * size.height * 4);
	for (unsigned b = 0; b < n; ++b)
		for (unsigned y = 0; y < tile; ++y)
			for (unsigned x = 0; x < tile; ++x) {
				const auto p = (y + halo) * patch + x + halo;
				const auto dst =
					((b / atlas_columns * tile + y) * size.width + b % atlas_columns * tile + x) *
					4;
				for (unsigned c = 0; c < 3; ++c)
					rgba[dst + c] = f.values[(b * 3 + c) * patch * patch + p];
				rgba[dst + 3] = a.values[b * patch * patch + p];
			}
	return rgba;
}
Stage::Stage(std::unique_ptr<ck2::Graph> graph, unsigned n, std::shared_ptr<const ModelReadLease> lease)
	: lease_(std::move(lease)),
	  graph_(std::move(graph)),
	  budget_(n)
{
	input_atlas(n);
	if (!graph_)
		throw std::invalid_argument("Missing refiner graph");
	device_ = static_cast<bool>(graph_->device_context()) || static_cast<bool>(graph_->dml_context());
	worker_ = std::jthread([this](std::stop_token stop) { work(stop); });
}
Stage::~Stage()
{
	{
		std::lock_guard lock(mutex_);
		worker_.request_stop();
	}
	wake_.notify_all();
	worker_.join();
}
bool Stage::submit(std::vector<float> &atlas)
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || busy_)
		return false;
	pending_ = std::move(atlas);
	busy_ = true;
	wake_.notify_one();
	return true;
}
bool Stage::submit_device(ID3D11Texture2D *atlas, ID3D11Texture2D *centres, std::function<bool()> wait)
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || busy_ || !device_ || !atlas || !centres)
		return false;
	pending_device_ = DeviceJob{atlas, centres, std::move(wait)};
	busy_ = true;
	wake_.notify_one();
	return true;
}
std::optional<Result> Stage::poll()
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || !result_)
		return {};
	auto result = std::move(result_);
	result_.reset();
	busy_ = false;
	return result;
}
void Stage::work(std::stop_token stop)
{
	for (;;) {
		std::vector<float> atlas;
		std::optional<DeviceJob> device_job;
		{
			std::unique_lock lock(mutex_);
			wake_.wait(lock, [this, stop] {
				return stop.stop_requested() || pending_.has_value() || pending_device_.has_value();
			});
			if (stop.stop_requested())
				return;
			if (pending_)
				atlas = std::move(*pending_);
			pending_.reset();
			device_job = std::exchange(pending_device_, {});
		}
		Result output;
		try {
			if (device_job && device_job->wait) {
				const auto started = Clock::now();
				if (!device_job->wait())
					throw std::runtime_error("Shared atlas completion timed out");
				output.wait_ms =
					std::chrono::duration<double, std::milli>(Clock::now() - started).count();
			}
			const auto unpack_start = Clock::now();
			ck2::Tensor x;
			if (device_job && graph_->dml_context()) {
				auto context = graph_->dml_context();
				auto &texture = dml_transfers_.get(context, device_job->atlas);
				auto rgba = texture.read();
				x.shape = {budget_, channels, patch, patch};
				x.directml = texture.conversion_buffer(static_cast<std::size_t>(budget_) * channels *
								       patch * patch * 4);
				const unsigned params[]{budget_, 0, 0, 0, 0, texture.pitch(), 0};
				context->compute("unpack_atlas", budget_ * channels * patch * patch, *rgba, nullptr,
						 *x.directml, params);
			} else if (device_job) {
				auto context = graph_->device_context();
				if (!input_texture_ || registered_.atlas != device_job->atlas ||
				    registered_.centres != device_job->centres) {
					input_texture_.reset();
					output_texture_.reset();
					input_texture_ =
						std::make_unique<nvidia::D3D11Texture>(context, device_job->atlas);
					output_texture_ =
						std::make_unique<nvidia::D3D11Texture>(context, device_job->centres);
					registered_ = DeviceJob{device_job->atlas, device_job->centres, {}};
				}
				x.shape = {budget_, channels, patch, patch};
				if (!device_input_)
					device_input_ = context->allocate(static_cast<std::size_t>(budget_) * channels *
									  patch * patch * sizeof(float));
				x.device = device_input_;
				auto rgba = input_texture_->read();
				auto src = rgba->pointer(), dst = x.device->pointer();
				void *args[]{&src, &dst, &budget_};
				context->launch("unpack_atlas", budget_ * channels * patch * patch, args);
				context->synchronize();
			} else
				x = unpack_atlas(atlas, budget_);
			const auto start = Clock::now();
			output.unpack_ms = std::chrono::duration<double, std::milli>(start - unpack_start).count();
			auto tensors = graph_->run({{"x", &x}}, stop);
			output.cuda_interval_ms = graph_->gpu_ms();
			const auto pack_start = Clock::now();
			output.inference_ms = std::chrono::duration<double, std::milli>(pack_start - start).count();
			if (device_job) {
				const auto &a = tensors.at("alpha"), &f = tensors.at("fg");
				ck2::check_tensor(a, ck2::Tensor({budget_, 1, patch, patch}, 0, false), true);
				ck2::check_tensor(f, ck2::Tensor({budget_, 3, patch, patch}, 0, false), true);
				if (a.directml && f.directml) {
					auto context = a.directml->context();
					auto &texture = dml_transfers_.get(context, device_job->centres);
					auto rgba = texture.conversion_buffer(static_cast<std::size_t>(budget_) * tile *
									      tile * 16);
					const unsigned params[]{budget_, 0, 0, 0, 0, 0, 0};
					context->compute("pack_centres", budget_ * tile * tile * 4, *f.directml,
							 a.directml.get(), *rgba, params);
					texture.write(*rgba);
				} else {
					if (!a.device || !f.device)
						throw std::runtime_error(
							"Refiner device output unavailable; retry staged atlas");
					auto context = a.device->context();
					if (!device_centres_)
						device_centres_ = context->allocate(static_cast<std::size_t>(budget_) *
										    tile * tile * 4 * sizeof(float));
					auto rgba = device_centres_;
					auto rgb = f.device->pointer(), alpha = a.device->pointer(),
					     dst = rgba->pointer();
					void *args[]{&rgb, &alpha, &dst, &budget_};
					context->launch("pack_centres", budget_ * tile * tile * 4, args);
					context->synchronize();
					output_texture_->write(*rgba);
				}
				output.device = true;
			} else {
				for (auto &[name, tensor] : tensors) {
					(void)name;
					tensor = ck2::download_tensor(tensor);
				}
				output.rgba = pack_centres(tensors, budget_);
			}
			output.pack_ms = std::chrono::duration<double, std::milli>(Clock::now() - pack_start).count();
		} catch (const std::exception &e) {
			output.failure = e.what();
		}
		std::lock_guard lock(mutex_);
		result_ = std::move(output);
	}
}
} // namespace corridorkey::refiner
