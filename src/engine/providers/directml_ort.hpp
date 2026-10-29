// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "directml.hpp"
#include "loader.hpp"
struct ID3D12Resource;
namespace corridorkey::dml {
// Stable prefix of OrtDmlApi from the documented DML provider factory header.
struct ProviderApi {
	OrtStatus *(ORT_API_CALL *append)(OrtSessionOptions *, int);
	OrtStatus *(ORT_API_CALL *append_device)(OrtSessionOptions *, IDMLDevice *, ID3D12CommandQueue *);
	OrtStatus *(ORT_API_CALL *create)(ID3D12Resource *, void **);
	OrtStatus *(ORT_API_CALL *free)(void *);
};
class TensorAllocation {
public:
	TensorAllocation(const ort::Runtime &, std::shared_ptr<Buffer>, std::span<const std::int64_t>);
	~TensorAllocation();
	TensorAllocation(const TensorAllocation &) = delete;
	TensorAllocation &operator=(const TensorAllocation &) = delete;
	OrtValue *value() const { return value_; }

private:
	const ort::Runtime &runtime_;
	std::shared_ptr<Buffer> buffer_;
	const ProviderApi *api_ = nullptr;
	void *allocation_ = nullptr;
	OrtValue *value_ = nullptr;
};
} // namespace corridorkey::dml
