// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck2_bindings.hpp"
#include "providers/directml_ort.hpp"
#include <algorithm>
#include <stdexcept>
namespace corridorkey::ck2 {
struct PersistentBindings::Impl {
	struct Buffer {
		std::string name;
		std::vector<std::int64_t> shape;
		std::vector<float> host;
		std::shared_ptr<nvidia::Buffer> device;
		std::shared_ptr<dml::Buffer> directml;
		std::unique_ptr<dml::TensorAllocation> dml_value;
		OrtValue *value = nullptr;
	};
	ort::Session &session;
	std::shared_ptr<nvidia::Context> cuda;
	std::vector<Buffer> inputs, outputs;
	std::vector<std::pair<std::string, const OrtValue *>> bound_inputs, bound_outputs;
	explicit Impl(ort::Session &owner) : session(owner) {}
	~Impl()
	{
		for (auto *buffers : {&inputs, &outputs})
			for (auto &buffer : *buffers)
				if (buffer.value && !buffer.dml_value)
					session.runtime().api().ReleaseValue(buffer.value);
	}
};
PersistentBindings::PersistentBindings(ort::Session &session, bool device) : impl_(std::make_unique<Impl>(session))
{
	auto &s = *impl_;
	const auto &rt = session.runtime();
	const auto &api = rt.api();
	if (device)
		s.cuda = session.device_context() ? session.device_context()
						  : std::make_shared<nvidia::Context>(session.device().ordinal);
	OrtMemoryInfo *memory = nullptr;
	if (device)
		rt.check(api.CreateMemoryInfo("Cuda", OrtDeviceAllocator, session.device().ordinal, OrtMemTypeDefault,
					      &memory));
	else
		rt.check(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
	std::unique_ptr<OrtMemoryInfo, decltype(api.ReleaseMemoryInfo)> guard(memory, api.ReleaseMemoryInfo);
	auto allocate = [&](const auto &specs, auto &buffers, auto &bindings) {
		buffers.reserve(specs.size());
		bindings.reserve(specs.size());
		for (const auto &spec : specs) {
			if (spec.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
				throw std::runtime_error("CK2 persistent I/O requires FP32 export tensors");
			Tensor tensor(spec.shape);
			buffers.emplace_back();
			auto &buffer = buffers.back();
			buffer.name = spec.name;
			buffer.shape = spec.shape;
			buffer.host = std::move(tensor.values);
			if (session.dml_context()) {
				buffer.directml = session.dml_context()->allocate(buffer.host.size() * sizeof(float));
				buffer.dml_value =
					std::make_unique<dml::TensorAllocation>(rt, buffer.directml, spec.shape);
				buffer.value = buffer.dml_value->value();
				bindings.emplace_back(spec.name, buffer.value);
				continue;
			}
			if (device)
				buffer.device = s.cuda->allocate(buffer.host.size() * sizeof(float));
			void *address =
				device ? reinterpret_cast<void *>(static_cast<std::uintptr_t>(buffer.device->pointer()))
				       : buffer.host.data();
			rt.check(api.CreateTensorWithDataAsOrtValue(memory, address, buffer.host.size() * sizeof(float),
								    spec.shape.data(), spec.shape.size(), spec.type,
								    &buffer.value));
			bindings.emplace_back(spec.name, buffer.value);
		}
	};
	allocate(session.inputs(), s.inputs, s.bound_inputs);
	allocate(session.outputs(), s.outputs, s.bound_outputs);
}
PersistentBindings::~PersistentBindings() = default;
void PersistentBindings::set(std::string_view name, std::span<const float> data)
{
	for (auto &buffer : impl_->inputs)
		if (buffer.name == name) {
			if (data.size() != buffer.host.size())
				throw std::invalid_argument("Persistent input size mismatch");
			if (buffer.directml)
				buffer.directml->upload(data.data(), data.size_bytes());
			else if (buffer.device)
				buffer.device->upload(data.data(), data.size_bytes());
			else
				std::copy(data.begin(), data.end(), buffer.host.begin());
			return;
		}
	throw std::invalid_argument("Unknown persistent input");
}
void PersistentBindings::set(std::string_view name, const Tensor &tensor)
{
	if (tensor.directml) {
		for (auto &buffer : impl_->inputs)
			if (buffer.name == name) {
				if (buffer.shape != tensor.shape)
					throw std::invalid_argument("DML tensor shape mismatch");
				if (buffer.directml)
					buffer.directml->copy(*tensor.directml);
				else {
					auto host = download_tensor(tensor);
					set(name, host.values);
				}
				return;
			}
		throw std::invalid_argument("Unknown DML input");
	}
	if (!tensor.device) {
		set(name, tensor.values);
		return;
	}
	for (auto &buffer : impl_->inputs)
		if (buffer.name == name) {
			if (buffer.shape != tensor.shape)
				throw std::invalid_argument("Persistent tensor shape mismatch");
			if (buffer.device)
				buffer.device->copy(*tensor.device);
			else {
				tensor.device->download(buffer.host.data(), buffer.host.size() * sizeof(float));
				if (buffer.directml)
					buffer.directml->upload(buffer.host.data(), buffer.host.size() * sizeof(float));
			}
			return;
		}
	throw std::invalid_argument("Unknown persistent input");
}
void PersistentBindings::run(std::stop_token stop)
{
	if (stop.stop_requested())
		throw std::runtime_error("Persistent inference cancelled");
	if (impl_->cuda)
		impl_->cuda->synchronize();
	if (impl_->cuda)
		impl_->cuda->start_timer();
	if (auto dml = impl_->session.dml_context()) {
		std::lock_guard lock(dml->mutex());
		try {
			impl_->session.run_bound(impl_->bound_inputs, impl_->bound_outputs, stop);
		} catch (...) {
			dml->synchronize();
			throw;
		}
		dml->synchronize();
	} else
		impl_->session.run_bound(impl_->bound_inputs, impl_->bound_outputs, stop);
	gpu_ms_ = impl_->cuda ? impl_->cuda->stop_timer() : -1;
}
void PersistentBindings::read(std::string_view name, std::span<float> data)
{
	for (auto &buffer : impl_->outputs)
		if (buffer.name == name) {
			if (data.size() != buffer.host.size())
				throw std::invalid_argument("Persistent output size mismatch");
			if (buffer.directml)
				buffer.directml->download(data.data(), data.size_bytes());
			else if (buffer.device)
				buffer.device->download(data.data(), data.size_bytes());
			else
				std::copy(buffer.host.begin(), buffer.host.end(), data.begin());
			return;
		}
	throw std::invalid_argument("Unknown persistent output");
}
Tensor PersistentBindings::device_output(std::string_view name)
{
	for (const auto &buffer : impl_->outputs)
		if (buffer.name == name && (buffer.device || buffer.directml)) {
			Tensor result;
			result.shape = buffer.shape;
			result.device = buffer.device;
			result.directml = buffer.directml;
			return result;
		}
	throw std::invalid_argument("Unknown persistent device output");
}
std::shared_ptr<dml::Context> PersistentBindings::dml_context() const
{
	return impl_->session.dml_context();
}
std::shared_ptr<nvidia::Context> PersistentBindings::context() const
{
	return impl_->cuda;
}
} // namespace corridorkey::ck2
