// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck2_bindings.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define CK_CUDA_CALL __stdcall
#else
#include <dlfcn.h>
#define CK_CUDA_CALL
#endif

namespace corridorkey::ck2 {
namespace {
using DevicePointer = unsigned long long;
struct Cuda {
	int(CK_CUDA_CALL *init)(unsigned) = nullptr;
	int(CK_CUDA_CALL *retain)(void **, int) = nullptr;
	int(CK_CUDA_CALL *release)(int) = nullptr;
	int(CK_CUDA_CALL *push)(void *) = nullptr;
	int(CK_CUDA_CALL *pop)(void **) = nullptr;
	int(CK_CUDA_CALL *allocate)(DevicePointer *, std::size_t) = nullptr;
	int(CK_CUDA_CALL *free)(DevicePointer) = nullptr;
	int(CK_CUDA_CALL *upload)(DevicePointer, const void *, std::size_t) = nullptr;
	int(CK_CUDA_CALL *download)(void *, DevicePointer, std::size_t) = nullptr;
	static void check(int result)
	{
		if (result)
			throw std::runtime_error("CUDA persistent buffer operation failed (" + std::to_string(result) +
						 ")");
	}
	Cuda()
	{
#ifdef _WIN32
		static auto module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		auto symbol = [](const char *name) {
			return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
		};
#else
		static auto module = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
		auto symbol = [](const char *name) {
			return module ? dlsym(module, name) : nullptr;
		};
#endif
		auto load = [&](auto &function, const char *name) {
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol(name));
			if (!function)
				throw std::runtime_error("CUDA driver lacks a required buffer API");
		};
		load(init, "cuInit");
		load(retain, "cuDevicePrimaryCtxRetain");
		load(release, "cuDevicePrimaryCtxRelease_v2");
		load(push, "cuCtxPushCurrent_v2");
		load(pop, "cuCtxPopCurrent_v2");
		load(allocate, "cuMemAlloc_v2");
		load(free, "cuMemFree_v2");
		load(upload, "cuMemcpyHtoD_v2");
		load(download, "cuMemcpyDtoH_v2");
		check(init(0));
	}
};
struct Context {
	Cuda *cuda;
	Context(Cuda *driver, void *context) : cuda(driver)
	{
		if (cuda)
			Cuda::check(cuda->push(context));
	}
	~Context()
	{
		if (cuda) {
			void *previous = nullptr;
			cuda->pop(&previous);
		}
	}
};
} // namespace
struct PersistentBindings::Impl {
	struct Buffer {
		std::string name;
		std::vector<float> host;
		DevicePointer device = 0;
		OrtValue *value = nullptr;
	};
	ort::Session &session;
	std::unique_ptr<Cuda> cuda;
	void *context = nullptr;
	int ordinal = 0;
	std::vector<Buffer> inputs, outputs;
	std::vector<std::pair<std::string, const OrtValue *>> bound_inputs, bound_outputs;
	explicit Impl(ort::Session &owner) : session(owner) {}
	~Impl()
	{
		try {
			Context scope(cuda.get(), context);
			for (auto *buffers : {&inputs, &outputs})
				for (auto &buffer : *buffers) {
					if (buffer.value)
						session.runtime().api().ReleaseValue(buffer.value);
					if (buffer.device)
						cuda->free(buffer.device);
				}
		} catch (...) {
		}
		if (context)
			cuda->release(ordinal);
	}
};
PersistentBindings::PersistentBindings(ort::Session &session, bool device) : impl_(std::make_unique<Impl>(session))
{
	auto &state = *impl_;
	const auto &rt = session.runtime();
	const auto &api = rt.api();
	if (device) {
		state.cuda = std::make_unique<Cuda>();
		state.ordinal = session.device().ordinal;
		Cuda::check(state.cuda->retain(&state.context, state.ordinal));
	}
	Context scope(state.cuda.get(), state.context);
	OrtMemoryInfo *memory = nullptr;
	if (device)
		rt.check(api.CreateMemoryInfo("Cuda", OrtDeviceAllocator, state.ordinal, OrtMemTypeDefault, &memory));
	else
		rt.check(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
	std::unique_ptr<OrtMemoryInfo, decltype(api.ReleaseMemoryInfo)> memory_guard(memory, api.ReleaseMemoryInfo);
	auto allocate = [&](const auto &specs, auto &buffers, auto &bindings) {
		buffers.reserve(specs.size());
		bindings.reserve(specs.size());
		for (const auto &spec : specs) {
			if (spec.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
				throw std::runtime_error("CK2 persistent I/O requires FP32 export tensors");
			std::size_t count = 1;
			for (auto dimension : spec.shape) {
				if (dimension <= 0 || static_cast<std::uint64_t>(dimension) > 64 * 1024 * 1024 / count)
					throw std::runtime_error("Persistent tensor exceeds size limit");
				count *= static_cast<std::size_t>(dimension);
			}
			buffers.emplace_back();
			auto &buffer = buffers.back();
			buffer.name = spec.name;
			buffer.host.resize(count);
			if (device)
				Cuda::check(state.cuda->allocate(&buffer.device, count * sizeof(float)));
			void *address = device ? reinterpret_cast<void *>(static_cast<std::uintptr_t>(buffer.device))
					       : buffer.host.data();
			rt.check(api.CreateTensorWithDataAsOrtValue(memory, address, count * sizeof(float),
								    spec.shape.data(), spec.shape.size(), spec.type,
								    &buffer.value));
			bindings.emplace_back(spec.name, buffer.value);
		}
	};
	allocate(session.inputs(), state.inputs, state.bound_inputs);
	allocate(session.outputs(), state.outputs, state.bound_outputs);
}
PersistentBindings::~PersistentBindings() = default;
void PersistentBindings::set(std::string_view name, std::span<const float> data)
{
	for (auto &buffer : impl_->inputs)
		if (buffer.name == name) {
			if (data.size() != buffer.host.size())
				throw std::invalid_argument("Persistent input size mismatch");
			std::copy(data.begin(), data.end(), buffer.host.begin());
			return;
		}
	throw std::invalid_argument("Unknown persistent input");
}
void PersistentBindings::run(std::stop_token stop)
{
	auto &state = *impl_;
	Context scope(state.cuda.get(), state.context);
	if (stop.stop_requested())
		throw std::runtime_error("Persistent inference cancelled");
	if (state.cuda)
		for (const auto &buffer : state.inputs)
			Cuda::check(state.cuda->upload(buffer.device, buffer.host.data(),
						       buffer.host.size() * sizeof(float)));
	state.session.run_bound(state.bound_inputs, state.bound_outputs, stop);
}
void PersistentBindings::read(std::string_view name, std::span<float> data)
{
	auto &state = *impl_;
	Context scope(state.cuda.get(), state.context);
	for (auto &buffer : state.outputs)
		if (buffer.name == name) {
			if (data.size() != buffer.host.size())
				throw std::invalid_argument("Persistent output size mismatch");
			if (state.cuda)
				Cuda::check(
					state.cuda->download(data.data(), buffer.device, data.size() * sizeof(float)));
			else
				std::copy(buffer.host.begin(), buffer.host.end(), data.begin());
			return;
		}
	throw std::invalid_argument("Unknown persistent output");
}
} // namespace corridorkey::ck2
