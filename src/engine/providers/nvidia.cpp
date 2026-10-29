// SPDX-License-Identifier: GPL-3.0-or-later
#include "nvidia.hpp"
#include <map>
#include <stdexcept>
#include <string>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define CK_CALL __stdcall
#else
#include <dlfcn.h>
#define CK_CALL
#endif
#include "gpu/cuda_kernels.inc"

namespace corridorkey::nvidia {
namespace {
void *driver_symbol(const char *name)
{
#ifdef _WIN32
	static auto library = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	return library ? reinterpret_cast<void *>(GetProcAddress(library, name)) : nullptr;
#else
	static auto library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
	return library ? dlsym(library, name) : nullptr;
#endif
}
template<typename T> T load(const char *name)
{
	auto function = reinterpret_cast<T>(driver_symbol(name));
	if (!function)
		throw std::runtime_error(std::string("CUDA driver API unavailable: ") + name);
	return function;
}
} // namespace
struct Context::Impl {
	int ordinal = 0;
	void *context = nullptr, *module = nullptr;
	void *stream = nullptr, *start = nullptr, *end = nullptr;
	std::map<std::string, void *> functions;
	std::mutex mutex;
	int(CK_CALL *init)(unsigned) = load<decltype(init)>("cuInit");
	int(CK_CALL *retain)(void **, int) = load<decltype(retain)>("cuDevicePrimaryCtxRetain");
	int(CK_CALL *release)(int) = load<decltype(release)>("cuDevicePrimaryCtxRelease_v2");
	int(CK_CALL *push)(void *) = load<decltype(push)>("cuCtxPushCurrent_v2");
	int(CK_CALL *pop)(void **) = load<decltype(pop)>("cuCtxPopCurrent_v2");
	int(CK_CALL *alloc)(Pointer *, std::size_t) = load<decltype(alloc)>("cuMemAlloc_v2");
	int(CK_CALL *free)(Pointer) = load<decltype(free)>("cuMemFree_v2");
	int(CK_CALL *upload)(Pointer, const void *, std::size_t) = load<decltype(upload)>("cuMemcpyHtoD_v2");
	int(CK_CALL *download)(void *, Pointer, std::size_t) = load<decltype(download)>("cuMemcpyDtoH_v2");
	int(CK_CALL *copy)(Pointer, Pointer, std::size_t) = load<decltype(copy)>("cuMemcpyDtoD_v2");
	int(CK_CALL *zero)(Pointer, unsigned char, std::size_t) = load<decltype(zero)>("cuMemsetD8_v2");
	int(CK_CALL *module_load)(void **, const void *) = load<decltype(module_load)>("cuModuleLoadData");
	int(CK_CALL *module_free)(void *) = load<decltype(module_free)>("cuModuleUnload");
	int(CK_CALL *function)(void **, void *, const char *) = load<decltype(function)>("cuModuleGetFunction");
	int(CK_CALL *launch)(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, void *,
			     void **, void **) = load<decltype(launch)>("cuLaunchKernel");
	int(CK_CALL *sync)(void *) = load<decltype(sync)>("cuStreamSynchronize");
	int(CK_CALL *stream_create)(void **, unsigned) = load<decltype(stream_create)>("cuStreamCreate");
	int(CK_CALL *stream_destroy)(void *) = load<decltype(stream_destroy)>("cuStreamDestroy_v2");
	int(CK_CALL *event_create)(void **, unsigned) = load<decltype(event_create)>("cuEventCreate");
	int(CK_CALL *event_destroy)(void *) = load<decltype(event_destroy)>("cuEventDestroy_v2");
	int(CK_CALL *event_record)(void *, void *) = load<decltype(event_record)>("cuEventRecord");
	int(CK_CALL *event_sync)(void *) = load<decltype(event_sync)>("cuEventSynchronize");
	int(CK_CALL *event_elapsed)(float *, void *, void *) = load<decltype(event_elapsed)>("cuEventElapsedTime");
};
void Context::check(int code, std::string_view operation)
{
	if (code)
		throw std::runtime_error(std::string(operation) + " failed (CUDA " + std::to_string(code) + ")");
}
Context::Scope::Scope(const Context &context) : context_(context)
{
	check(context_.impl_->push(context_.impl_->context), "Push CUDA context");
}
Context::Scope::~Scope()
{
	void *previous = nullptr;
	context_.impl_->pop(&previous);
}
Context::Context(int ordinal) : impl_(std::make_unique<Impl>())
{
	impl_->ordinal = ordinal;
	check(impl_->init(0), "Initialize CUDA");
	check(impl_->retain(&impl_->context, ordinal), "Retain CUDA context");
	try {
		Scope scope(*this);
		check(impl_->stream_create(&impl_->stream, 0), "Create CUDA stream");
		check(impl_->event_create(&impl_->start, 0), "Create CUDA timing event");
		check(impl_->event_create(&impl_->end, 0), "Create CUDA timing event");
	} catch (...) {
		{
			Scope scope(*this);
			if (impl_->start)
				impl_->event_destroy(impl_->start);
			if (impl_->stream)
				impl_->stream_destroy(impl_->stream);
		}
		impl_->release(ordinal);
		throw;
	}
}
void Context::ensure_kernels()
{
	Scope scope(*this);
	std::lock_guard lock(impl_->mutex);
	if (!impl_->module)
		check(impl_->module_load(&impl_->module, kernel_ptx), "Load conversion kernels");
}
Context::~Context()
{
	try {
		Scope scope(*this);
		impl_->sync(impl_->stream);
		if (impl_->module)
			impl_->module_free(impl_->module);
		impl_->event_destroy(impl_->start);
		impl_->event_destroy(impl_->end);
		impl_->stream_destroy(impl_->stream);
	} catch (...) {
	}
	impl_->release(impl_->ordinal);
}
int Context::ordinal() const
{
	return impl_->ordinal;
}
void *Context::symbol(const char *name) const
{
	return driver_symbol(name);
}
std::shared_ptr<Buffer> Context::allocate(std::size_t bytes)
{
	return std::make_shared<Buffer>(shared_from_this(), bytes);
}
void Context::launch(const char *name, unsigned count, std::span<void *> arguments)
{
	Scope scope(*this);
	ensure_kernels();
	std::lock_guard lock(impl_->mutex);
	auto &function = impl_->functions[name];
	if (!function)
		check(impl_->function(&function, impl_->module, name), "Find CUDA kernel");
	check(impl_->launch(function, (count + 255) / 256, 1, 1, 256, 1, 1, 0, impl_->stream, arguments.data(),
			    nullptr),
	      name);
}
void Context::synchronize()
{
	Scope scope(*this);
	check(impl_->sync(impl_->stream), "Complete conversion stream");
}
void *Context::stream() const
{
	return impl_->stream;
}
void Context::start_timer()
{
	Scope scope(*this);
	check(impl_->event_record(impl_->start, impl_->stream), "Record CUDA start");
	check(impl_->event_sync(impl_->start), "Begin CUDA interval");
}
double Context::stop_timer()
{
	Scope scope(*this);
	check(impl_->event_record(impl_->end, impl_->stream), "Record CUDA end");
	check(impl_->event_sync(impl_->end), "Complete CUDA timer");
	float milliseconds = 0;
	check(impl_->event_elapsed(&milliseconds, impl_->start, impl_->end), "Read CUDA duration");
	return milliseconds;
}
void Context::validate(const Buffer &buffer, bool unit_range)
{
	auto invalid = allocate(sizeof(unsigned));
	invalid->zero();
	auto src = buffer.pointer(), flag = invalid->pointer();
	auto count = static_cast<unsigned>(buffer.bytes() / sizeof(float));
	unsigned unit = unit_range ? 1 : 0;
	void *args[]{&src, &count, &unit, &flag};
	launch("validate", count, args);
	unsigned bad = 0;
	invalid->download(&bad, sizeof(bad));
	if (bad)
		throw std::runtime_error("CUDA tensor has invalid values");
}
Buffer::Buffer(std::shared_ptr<Context> context, std::size_t bytes) : context_(std::move(context)), bytes_(bytes)
{
	if (!bytes || bytes > 256ULL * 1024 * 1024)
		throw std::invalid_argument("Invalid CUDA allocation size");
	Context::Scope scope(*context_);
	Context::check(context_->impl_->alloc(&pointer_, bytes), "Allocate CUDA buffer");
}
Buffer::~Buffer()
{
	try {
		Context::Scope scope(*context_);
		context_->impl_->free(pointer_);
	} catch (...) {
	}
}
void Buffer::upload(const void *data, std::size_t bytes)
{
	if (bytes != bytes_)
		throw std::invalid_argument("CUDA upload size mismatch");
	Context::Scope scope(*context_);
	Context::check(context_->impl_->upload(pointer_, data, bytes), "Upload CUDA buffer");
}
void Buffer::download(void *data, std::size_t bytes) const
{
	if (bytes != bytes_)
		throw std::invalid_argument("CUDA download size mismatch");
	Context::Scope scope(*context_);
	Context::check(context_->impl_->download(data, pointer_, bytes), "Download CUDA buffer");
}
void Buffer::copy(const Buffer &source)
{
	if (bytes_ != source.bytes_ || context_->ordinal() != source.context_->ordinal())
		throw std::invalid_argument("CUDA copy size/device mismatch");
	Context::Scope scope(*context_);
	Context::check(context_->impl_->copy(pointer_, source.pointer_, bytes_), "Copy CUDA buffer");
}
void Buffer::zero()
{
	Context::Scope scope(*context_);
	Context::check(context_->impl_->zero(pointer_, 0, bytes_), "Zero CUDA buffer");
}
} // namespace corridorkey::nvidia
