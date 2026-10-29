// SPDX-License-Identifier: GPL-3.0-or-later
#include "directml.hpp"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <array>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <DirectML.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#endif
namespace corridorkey::dml {
void Context::probe()
{
	const std::array<std::uint32_t, 4> pixels{0xff030201U, 0xffaa5511U, 0xff00ff80U, 0xffffffffU};
	auto packed = allocate(sizeof(pixels));
	packed->upload(pixels.data(), sizeof(pixels));
	auto planar = allocate(12 * sizeof(float));
	const unsigned params[]{4, 1, 0, 0, 0, 16, 0};
	compute("rgba_to_planar", 12, *packed, nullptr, *planar, params);
	std::array<float, 12> actual{};
	planar->download(actual.data(), sizeof(actual));
	for (unsigned c = 0; c < 3; ++c)
		for (unsigned p = 0; p < 4; ++p) {
			const float expected = static_cast<float>((pixels[p] >> (c * 8)) & 255) / 255.f;
			if (!std::isfinite(actual[c * 4 + p]) || std::abs(actual[c * 4 + p] - expected) > 1e-7f)
				throw std::runtime_error("DML buffer conversion self-test failed");
		}
	validate(*planar, true);
	// Exercise ties-to-even and subnormal rounding used by the refiner contract.
	const std::array<float, 16> atlas{0.15252685546875f, 0, 0, 0, 1.f / 33554432,  0, 0, 0,
					  3.f / 33554432,    0, 0, 0, 0.500244140625f, 0, 0, 0};
	auto raw = allocate(sizeof(atlas));
	raw->upload(atlas.data(), sizeof(atlas));
	auto rounded = allocate(4 * sizeof(float));
	const unsigned atlas_params[]{16, 0, 0, 0, 0, 832 * 16, 0};
	compute("unpack_atlas", 4, *raw, nullptr, *rounded, atlas_params);
	std::array<float, 4> result{};
	rounded->download(result.data(), sizeof(result));
	const std::array<float, 4> expected{0.152587890625f, 0, 2.f / 16777216, 0.5f};
	if (result != expected)
		throw std::runtime_error("DML refiner rounding self-test failed");
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr, const char *operation)
{
	if (FAILED(hr)) {
		std::ostringstream message;
		message << operation << " (0x" << std::hex << static_cast<unsigned>(hr) << ')';
		throw std::runtime_error(message.str());
	}
}
struct Library {
	HMODULE module = nullptr;
	explicit Library(const std::filesystem::path &path)
	{
		module = LoadLibraryExW(path.c_str(), nullptr,
					path.is_absolute()
						? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32
						: LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!module)
			throw std::runtime_error("DML transport library is unavailable");
	}
	~Library() { FreeLibrary(module); }
	template<class T> T symbol(const char *name) const
	{
		auto p = reinterpret_cast<T>(GetProcAddress(module, name));
		if (!p)
			throw std::runtime_error("DML transport API is unavailable");
		return p;
	}
};
constexpr const char *shaders =
#include "gpu/dml_kernels.inc"
	;
ComPtr<ID3D12Resource> buffer(ID3D12Device *device, std::size_t bytes, D3D12_HEAP_TYPE heap)
{
	D3D12_HEAP_PROPERTIES properties{};
	properties.Type = heap;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = (bytes + 3) & ~std::size_t(3);
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
						     : D3D12_RESOURCE_FLAG_NONE;
	auto state = heap == D3D12_HEAP_TYPE_DEFAULT  ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		     : heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
						      : D3D12_RESOURCE_STATE_COPY_DEST;
	ComPtr<ID3D12Resource> result;
	check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
					      IID_PPV_ARGS(&result)),
	      "Allocate D3D12 buffer");
	return result;
}
void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
	list->ResourceBarrier(1, &barrier);
}
} // namespace
struct Context::Impl {
	Library d3d12{"d3d12.dll"}, compiler{"d3dcompiler_47.dll"};
	std::unique_ptr<Library> dml;
	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<ID3D12Device> device;
	ComPtr<IDMLDevice> ml;
	ComPtr<ID3D12CommandQueue> queue;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> list;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12Resource> validation_flag, validation_zero, validation_readback;
	std::map<std::string, ComPtr<ID3D12PipelineState>> kernels;
	std::recursive_mutex mutex;
	HANDLE event = nullptr;
	UINT64 sequence = 0;
	std::atomic<bool> failed{false};
	// Retain every resource referenced by the current submission through its fence.
	std::vector<ComPtr<ID3D12Resource>> pending;
	~Impl()
	{
		if (event)
			CloseHandle(event);
	}
};
struct Buffer::Impl {
	ComPtr<ID3D12Resource> resource, upload, readback;
};
Context::Context(const std::string &luid, const std::filesystem::path &library) : impl_(std::make_unique<Impl>())
{
	auto &s = *impl_;
	ComPtr<IDXGIFactory1> factory;
	check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create DXGI factory");
	for (UINT i = 0;; ++i) {
		ComPtr<IDXGIAdapter1> candidate;
		if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 desc{};
		check(candidate->GetDesc1(&desc), "Read adapter identity");
		std::ostringstream id;
		id << "luid:" << std::hex << std::setfill('0') << std::setw(8)
		   << static_cast<unsigned>(desc.AdapterLuid.HighPart) << ':' << std::setw(8)
		   << desc.AdapterLuid.LowPart;
		if (id.str() == luid && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
			s.adapter = candidate;
			break;
		}
	}
	if (!s.adapter)
		throw std::runtime_error("No D3D12 adapter matches OBS LUID");
	check(s.d3d12.symbol<decltype(&D3D12CreateDevice)>("D3D12CreateDevice")(s.adapter.Get(), D3D_FEATURE_LEVEL_11_0,
										IID_PPV_ARGS(&s.device)),
	      "Create matched D3D12 device");
	s.dml = std::make_unique<Library>(library);
	check(s.dml->symbol<decltype(&DMLCreateDevice)>("DMLCreateDevice")(s.device.Get(), DML_CREATE_DEVICE_FLAG_NONE,
									   IID_PPV_ARGS(&s.ml)),
	      "Create DirectML device");
	D3D12_COMMAND_QUEUE_DESC queue{};
	queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	check(s.device->CreateCommandQueue(&queue, IID_PPV_ARGS(&s.queue)), "Create DML queue");
	check(s.device->CreateCommandAllocator(queue.Type, IID_PPV_ARGS(&s.allocator)), "Create DML allocator");
	check(s.device->CreateCommandList(0, queue.Type, s.allocator.Get(), nullptr, IID_PPV_ARGS(&s.list)),
	      "Create DML list");
	check(s.list->Close(), "Close DML list");
	check(s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence)), "Create DML fence");
	s.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!s.event)
		throw std::runtime_error("Cannot create DML completion event");
	D3D12_ROOT_PARAMETER parameters[4]{};
	parameters[0].ParameterType = parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[1].Descriptor.ShaderRegister = 1;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	parameters[2].Descriptor.ShaderRegister = 0;
	parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[3].Constants = {0, 0, 8};
	D3D12_ROOT_SIGNATURE_DESC desc{4, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
	ComPtr<ID3DBlob> blob, error;
	check(s.d3d12.symbol<decltype(&D3D12SerializeRootSignature)>(
		      "D3D12SerializeRootSignature")(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
	      "Serialize DML compute root");
	check(s.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s.root)),
	      "Create compute root");
	for (const char *name :
	     {"rgba_to_planar", "planar_to_rgba", "resize_area", "validate", "unpack_atlas", "pack_centres"}) {
		blob.Reset();
		error.Reset();
		auto hr = s.compiler.symbol<decltype(&D3DCompile)>(
			"D3DCompile")(shaders, std::strlen(shaders), "dml_kernels", nullptr, nullptr, name, "cs_5_0",
				      D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0, &blob, &error);
		if (FAILED(hr) && error)
			throw std::runtime_error(static_cast<const char *>(error->GetBufferPointer()));
		check(hr, "Compile DML conversion kernel");
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
		pipeline.pRootSignature = s.root.Get();
		pipeline.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
		check(s.device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&s.kernels[name])),
		      "Create DML conversion pipeline");
	}
}
Context::~Context()
{
	// A timed-out queue may still reference these resources. Quarantine its owner
	// for process lifetime instead of freeing GPU memory beneath an executing list.
	if (impl_ && impl_->failed && impl_->fence && impl_->fence->GetCompletedValue() < impl_->sequence)
		(void)impl_.release();
}
bool Context::failed() const noexcept
{
	return impl_->failed.load();
}
ID3D12Device *Context::device() const
{
	return impl_->device.Get();
}
IDMLDevice *Context::dml_device() const
{
	return impl_->ml.Get();
}
ID3D12CommandQueue *Context::queue() const
{
	return impl_->queue.Get();
}
IDXGIAdapter *Context::adapter() const
{
	return impl_->adapter.Get();
}
std::recursive_mutex &Context::mutex()
{
	return impl_->mutex;
}
ID3D12GraphicsCommandList *Context::begin()
{
	if (impl_->failed)
		throw std::runtime_error("DML queue is unavailable after a fence failure");
	check(impl_->allocator->Reset(), "Reset DML allocator");
	check(impl_->list->Reset(impl_->allocator.Get(), nullptr), "Reset DML list");
	return impl_->list.Get();
}
void Context::finish()
{
	check(impl_->list->Close(), "Close DML commands");
	ID3D12CommandList *lists[]{impl_->list.Get()};
	impl_->queue->ExecuteCommandLists(1, lists);
	synchronize();
}
void Context::synchronize()
{
	std::lock_guard lock(mutex());
	auto &s = *impl_;
	if (s.failed)
		throw std::runtime_error("DML queue is unavailable");
	try {
		check(s.queue->Signal(s.fence.Get(), ++s.sequence), "Signal DML completion");
		check(s.fence->SetEventOnCompletion(s.sequence, s.event), "Arm DML completion");
		if (WaitForSingleObject(s.event, 10000) != WAIT_OBJECT_0)
			throw std::runtime_error("DML completion timed out");
		check(s.device->GetDeviceRemovedReason(), "DML device removed");
		s.pending.clear();
	} catch (...) {
		s.failed = true;
		throw;
	}
}
std::shared_ptr<Buffer> Context::allocate(std::size_t bytes)
{
	return std::make_shared<Buffer>(shared_from_this(), bytes);
}
Buffer::Buffer(std::shared_ptr<Context> context, std::size_t bytes)
	: context_(std::move(context)),
	  impl_(std::make_unique<Impl>()),
	  bytes_(bytes)
{
	if (!bytes || bytes > 1024ULL * 1024 * 1024 || bytes % 4)
		throw std::invalid_argument("Invalid DML buffer size");
	impl_->resource = buffer(context_->device(), bytes, D3D12_HEAP_TYPE_DEFAULT);
}
Buffer::~Buffer()
{
	if (context_ && context_->failed())
		(void)impl_.release();
}
ID3D12Resource *Buffer::resource() const
{
	return impl_->resource.Get();
}
void Buffer::upload(const void *source, std::size_t bytes)
{
	if (!source || bytes != bytes_)
		throw std::invalid_argument("DML upload size mismatch");
	std::lock_guard lock(context_->mutex());
	if (!impl_->upload)
		impl_->upload = buffer(context_->device(), bytes_, D3D12_HEAP_TYPE_UPLOAD);
	void *mapped = nullptr;
	D3D12_RANGE empty{};
	check(impl_->upload->Map(0, &empty, &mapped), "Map DML upload");
	std::memcpy(mapped, source, bytes);
	impl_->upload->Unmap(0, nullptr);
	auto *list = context_->begin();
	context_->impl_->pending = {impl_->resource, impl_->upload};
	transition(list, resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
	list->CopyBufferRegion(resource(), 0, impl_->upload.Get(), 0, bytes);
	transition(list, resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context_->finish();
}
void Buffer::download(void *target, std::size_t bytes) const
{
	if (!target || bytes != bytes_)
		throw std::invalid_argument("DML download size mismatch");
	std::lock_guard lock(context_->mutex());
	if (!impl_->readback)
		impl_->readback = buffer(context_->device(), bytes_, D3D12_HEAP_TYPE_READBACK);
	auto *list = context_->begin();
	context_->impl_->pending = {impl_->resource, impl_->readback};
	transition(list, resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	list->CopyBufferRegion(impl_->readback.Get(), 0, resource(), 0, bytes);
	transition(list, resource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context_->finish();
	void *mapped = nullptr;
	D3D12_RANGE range{0, bytes};
	check(impl_->readback->Map(0, &range, &mapped), "Map DML readback");
	std::memcpy(target, mapped, bytes);
	D3D12_RANGE empty{};
	impl_->readback->Unmap(0, &empty);
}
void Buffer::copy(const Buffer &source)
{
	if (source.bytes() != bytes_ || source.context() != context_)
		throw std::invalid_argument("DML copy mismatch");
	if (&source == this)
		return;
	std::lock_guard lock(context_->mutex());
	auto *list = context_->begin();
	context_->impl_->pending = {impl_->resource, source.impl_->resource};
	transition(list, source.resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	transition(list, resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
	list->CopyBufferRegion(resource(), 0, source.resource(), 0, bytes_);
	transition(list, source.resource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	transition(list, resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context_->finish();
}
void Buffer::zero()
{
	std::vector<std::uint32_t> zeros(bytes_ / 4);
	upload(zeros.data(), bytes_);
}
void Context::compute(const char *kernel, unsigned count, const Buffer &src, const Buffer *second, Buffer &dst,
		      std::span<const unsigned> constants)
{
	if (src.context().get() != this || dst.context().get() != this || (second && second->context().get() != this) ||
	    constants.size() != 7 || !count || &src == &dst || second == &dst)
		throw std::invalid_argument("DML compute binding mismatch");
	std::lock_guard lock(mutex());
	auto *list = begin();
	impl_->pending = {src.resource(), dst.resource()};
	if (second)
		impl_->pending.emplace_back(second->resource());
	transition(list, src.resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	if (second && second != &src)
		transition(list, second->resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	list->SetComputeRootSignature(impl_->root.Get());
	list->SetPipelineState(impl_->kernels.at(kernel).Get());
	list->SetComputeRootShaderResourceView(0, src.resource()->GetGPUVirtualAddress());
	list->SetComputeRootShaderResourceView(1, (second ? second : &src)->resource()->GetGPUVirtualAddress());
	list->SetComputeRootUnorderedAccessView(2, dst.resource()->GetGPUVirtualAddress());
	list->SetComputeRoot32BitConstant(3, count, 0);
	list->SetComputeRoot32BitConstants(3, 7, constants.data(), 1);
	const auto groups = (count + 255) / 256;
	list->Dispatch(std::min(groups, 65535U), (groups + 65534) / 65535, 1);
	transition(list, src.resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	if (second && second != &src)
		transition(list, second->resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = dst.resource();
	list->ResourceBarrier(1, &barrier);
	finish();
}
void Context::validate(const Buffer &source, bool unit)
{
	if (source.context().get() != this)
		throw std::invalid_argument("DML validation device mismatch");
	std::lock_guard lock(mutex());
	auto &s = *impl_;
	if (!s.validation_flag) {
		s.validation_flag = buffer(device(), 4, D3D12_HEAP_TYPE_DEFAULT);
		s.validation_zero = buffer(device(), 4, D3D12_HEAP_TYPE_UPLOAD);
		s.validation_readback = buffer(device(), 4, D3D12_HEAP_TYPE_READBACK);
		void *mapped = nullptr;
		D3D12_RANGE empty{};
		check(s.validation_zero->Map(0, &empty, &mapped), "Map validation reset");
		std::memset(mapped, 0, 4);
		s.validation_zero->Unmap(0, nullptr);
	}
	auto *list = begin();
	s.pending = {source.resource(), s.validation_flag, s.validation_zero, s.validation_readback};
	transition(list, s.validation_flag.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		   D3D12_RESOURCE_STATE_COPY_DEST);
	list->CopyBufferRegion(s.validation_flag.Get(), 0, s.validation_zero.Get(), 0, 4);
	transition(list, s.validation_flag.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	transition(list, source.resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	list->SetComputeRootSignature(s.root.Get());
	list->SetPipelineState(s.kernels.at("validate").Get());
	list->SetComputeRootShaderResourceView(0, source.resource()->GetGPUVirtualAddress());
	list->SetComputeRootShaderResourceView(1, source.resource()->GetGPUVirtualAddress());
	list->SetComputeRootUnorderedAccessView(2, s.validation_flag->GetGPUVirtualAddress());
	const unsigned count = static_cast<unsigned>(source.bytes() / 4);
	const unsigned params[]{count, unit ? 1U : 0U, 0, 0, 0, 0, 0, 0};
	list->SetComputeRoot32BitConstants(3, 8, params, 0);
	const unsigned groups = (count + 255) / 256;
	list->Dispatch(std::min(groups, 65535U), (groups + 65534) / 65535, 1);
	transition(list, source.resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	transition(list, s.validation_flag.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		   D3D12_RESOURCE_STATE_COPY_SOURCE);
	list->CopyBufferRegion(s.validation_readback.Get(), 0, s.validation_flag.Get(), 0, 4);
	transition(list, s.validation_flag.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
		   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	finish();
	void *mapped = nullptr;
	D3D12_RANGE range{0, 4};
	check(s.validation_readback->Map(0, &range, &mapped), "Read tensor validation flag");
	const auto invalid = *static_cast<const unsigned *>(mapped);
	D3D12_RANGE empty{};
	s.validation_readback->Unmap(0, &empty);
	if (invalid)
		throw std::runtime_error("DML tensor has invalid values");
}
#else
struct Context::Impl {};
struct Buffer::Impl {};
Context::Context(const std::string &, const std::filesystem::path &)
{
	throw std::runtime_error("DirectML requires Windows");
}
Context::~Context() = default;
bool Context::failed() const noexcept
{
	return true;
}
ID3D12Device *Context::device() const
{
	return nullptr;
}
IDMLDevice *Context::dml_device() const
{
	return nullptr;
}
ID3D12CommandQueue *Context::queue() const
{
	return nullptr;
}
IDXGIAdapter *Context::adapter() const
{
	return nullptr;
}
std::recursive_mutex &Context::mutex()
{
	throw std::runtime_error("DirectML requires Windows");
}
ID3D12GraphicsCommandList *Context::begin()
{
	throw std::runtime_error("DirectML requires Windows");
}
void Context::finish()
{
	throw std::runtime_error("DirectML requires Windows");
}
void Context::synchronize()
{
	throw std::runtime_error("DirectML requires Windows");
}
std::shared_ptr<Buffer> Context::allocate(std::size_t)
{
	throw std::runtime_error("DirectML requires Windows");
}
void Context::compute(const char *, unsigned, const Buffer &, const Buffer *, Buffer &, std::span<const unsigned>)
{
	throw std::runtime_error("DirectML requires Windows");
}
void Context::validate(const Buffer &, bool)
{
	throw std::runtime_error("DirectML requires Windows");
}
Buffer::Buffer(std::shared_ptr<Context>, std::size_t bytes) : bytes_(bytes)
{
	throw std::runtime_error("DirectML requires Windows");
}
Buffer::~Buffer()
{
	if (context_ && context_->failed())
		(void)impl_.release();
}
ID3D12Resource *Buffer::resource() const
{
	return nullptr;
}
void Buffer::upload(const void *, std::size_t)
{
	throw std::runtime_error("DirectML requires Windows");
}
void Buffer::download(void *, std::size_t) const
{
	throw std::runtime_error("DirectML requires Windows");
}
void Buffer::copy(const Buffer &)
{
	throw std::runtime_error("DirectML requires Windows");
}
void Buffer::zero()
{
	throw std::runtime_error("DirectML requires Windows");
}
#endif
} // namespace corridorkey::dml
