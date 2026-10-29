// SPDX-License-Identifier: GPL-3.0-or-later
#include "directml_ort.hpp"
#include <stdexcept>
namespace corridorkey::dml {
TensorAllocation::TensorAllocation(const ort::Runtime &runtime, std::shared_ptr<Buffer> buffer,
				   std::span<const std::int64_t> shape)
	: runtime_(runtime),
	  buffer_(std::move(buffer))
{
	std::size_t count = 1;
	if (shape.empty() || !buffer_)
		throw std::invalid_argument("Invalid DML tensor");
	for (auto dim : shape) {
		if (dim <= 0 || static_cast<std::uint64_t>(dim) > buffer_->bytes() / 4 / count)
			throw std::invalid_argument("Invalid DML tensor dimensions");
		count *= static_cast<std::size_t>(dim);
	}
	if (count * 4 != buffer_->bytes())
		throw std::invalid_argument("DML tensor allocation size mismatch");
	const void *extension = nullptr;
	runtime.check(runtime.api().GetExecutionProviderApi("DML", runtime.api_version(), &extension));
	api_ = static_cast<const ProviderApi *>(extension);
	if (!api_ || !api_->create || !api_->free)
		throw std::runtime_error("DML device-resource API unavailable");
	OrtMemoryInfo *memory = nullptr;
	runtime.check(runtime.api().CreateMemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault, &memory));
	std::unique_ptr<OrtMemoryInfo, decltype(runtime.api().ReleaseMemoryInfo)> guard(
		memory, runtime.api().ReleaseMemoryInfo);
	runtime.check(api_->create(buffer_->resource(), &allocation_));
	try {
		runtime.check(runtime.api().CreateTensorWithDataAsOrtValue(
			memory, allocation_, buffer_->bytes(), shape.data(), shape.size(),
			ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &value_));
	} catch (...) {
		if (auto status = api_->free(allocation_))
			runtime.api().ReleaseStatus(status);
		allocation_ = nullptr;
		throw;
	}
}
TensorAllocation::~TensorAllocation()
{
	if (buffer_->context()->failed())
		return;
	if (value_)
		runtime_.api().ReleaseValue(value_);
	if (allocation_)
		if (auto status = api_->free(allocation_))
			runtime_.api().ReleaseStatus(status);
}
} // namespace corridorkey::dml
