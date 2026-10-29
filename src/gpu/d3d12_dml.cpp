// SPDX-License-Identifier: GPL-3.0-or-later
#include "d3d12_dml.hpp"
#include <stdexcept>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>
#endif
namespace corridorkey::dml {
Texture &TextureTransfers::get(std::shared_ptr<Context> context, ID3D11Texture2D *texture)
{
	const auto key = std::pair{context.get(), texture};
	if (const auto it = textures_.find(key); it != textures_.end())
		return *it->second;
	if (textures_.size() >= 32)
		textures_.clear();
	auto entry = std::make_unique<Texture>(std::move(context), texture);
	auto &result = *entry;
	textures_.emplace(key, std::move(entry));
	return result;
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr, const char *operation)
{
	if (FAILED(hr)) {
		std::ostringstream message;
		message << operation << " (0x" << std::hex << static_cast<unsigned>(hr) << ")";
		throw std::runtime_error(message.str());
	}
}
struct WorkerDevice {
	ComPtr<ID3D11Device5> device;
	ComPtr<ID3D11DeviceContext4> immediate;
	explicit WorkerDevice(IDXGIAdapter *adapter)
	{
		ComPtr<ID3D11Device> base;
		ComPtr<ID3D11DeviceContext> commands;
		check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
					nullptr, 0, D3D11_SDK_VERSION, &base, nullptr, &commands),
		      "Create worker D3D11 bridge");
		check(base.As(&device), "D3D11 shared fences unavailable");
		check(commands.As(&immediate), "D3D11 context fences unavailable");
	}
};
std::mutex worker_mutex;
std::map<Context *, std::weak_ptr<WorkerDevice>> workers;
std::shared_ptr<WorkerDevice> worker_for(Context &context)
{
	std::lock_guard lock(worker_mutex);
	std::erase_if(workers, [](const auto &entry) { return entry.second.expired(); });
	auto worker = workers[&context].lock();
	if (!worker) {
		worker = std::make_shared<WorkerDevice>(context.adapter());
		workers[&context] = worker;
	}
	return worker;
}
struct Handle {
	HANDLE value = nullptr;
	~Handle()
	{
		if (value)
			CloseHandle(value);
	}
};
void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
	list->ResourceBarrier(1, &b);
}
} // namespace
struct Texture::Impl {
	std::shared_ptr<Context> context;
	std::shared_ptr<WorkerDevice> worker;
	ComPtr<ID3D11Device5> device;
	ComPtr<ID3D11DeviceContext4> immediate;
	ComPtr<ID3D11Texture2D> original, alias, bridge;
	ComPtr<ID3D12Resource> shared;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D11Fence> fence11;
	std::shared_ptr<Buffer> linear, conversion;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	unsigned width = 0, height = 0, format = 0;
	UINT64 sequence = 0;
	Handle event;
	bool submitted = false;
	void transfer(const Buffer &buffer, bool output)
	{
		if (buffer.context() != context ||
		    buffer.bytes() != static_cast<std::size_t>(footprint.Footprint.RowPitch) * height)
			throw std::invalid_argument("DML texture buffer mismatch");
		std::lock_guard lock(context->mutex());
		submitted = true;
		if (!output) {
			immediate->CopyResource(bridge.Get(), alias.Get());
			check(immediate->Signal(fence11.Get(), ++sequence), "Signal shared D3D11 input fence");
			immediate->Flush();
			check(context->queue()->Wait(fence.Get(), sequence), "Wait for shared D3D11 input");
		}
		auto *list = context->begin();
		const auto buffer_state = output ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
		const auto texture_state = output ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COPY_SOURCE;
		transition(list, buffer.resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, buffer_state);
		transition(list, shared.Get(), D3D12_RESOURCE_STATE_COMMON, texture_state);
		D3D12_TEXTURE_COPY_LOCATION tex{}, buf{};
		tex.pResource = shared.Get();
		tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		buf.pResource = buffer.resource();
		buf.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		buf.PlacedFootprint = footprint;
		list->CopyTextureRegion(output ? &tex : &buf, 0, 0, 0, output ? &buf : &tex, nullptr);
		transition(list, buffer.resource(), buffer_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		transition(list, shared.Get(), texture_state, D3D12_RESOURCE_STATE_COMMON);
		context->finish();
		if (output) {
			check(context->queue()->Signal(fence.Get(), ++sequence), "Signal shared D3D12 output fence");
			check(immediate->Wait(fence11.Get(), sequence), "Wait for shared D3D12 output");
			immediate->CopyResource(alias.Get(), bridge.Get());
			check(immediate->Signal(fence11.Get(), ++sequence), "Signal D3D11 publication");
			immediate->Flush();
			check(fence->SetEventOnCompletion(sequence, event.value), "Arm D3D11 publication fence");
			if (WaitForSingleObject(event.value, 10000) != WAIT_OBJECT_0)
				throw std::runtime_error("D3D11 publication timed out");
			check(device->GetDeviceRemovedReason(), "D3D11 bridge device removed");
		}
		submitted = false;
	}
};
Texture::Texture(std::shared_ptr<Context> context, ID3D11Texture2D *texture) : impl_(std::make_unique<Impl>())
{
	if (!context || !texture)
		throw std::invalid_argument("Missing DML shared texture");
	auto &s = *impl_;
	s.context = std::move(context);
	s.original = texture;
	std::lock_guard lock(s.context->mutex());
	ComPtr<ID3D11Device> original_device;
	texture->GetDevice(&original_device);
	ComPtr<IDXGIDevice> dxgi;
	ComPtr<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC desc{};
	check(original_device.As(&dxgi), "Identify OBS D3D11 device");
	check(dxgi->GetAdapter(&adapter), "Identify OBS adapter");
	check(adapter->GetDesc(&desc), "Read OBS adapter LUID");
	const auto luid = s.context->device()->GetAdapterLuid();
	if (std::memcmp(&luid, &desc.AdapterLuid, sizeof(luid)))
		throw std::runtime_error("OBS and DML adapter LUIDs differ");
	D3D11_TEXTURE2D_DESC source{};
	texture->GetDesc(&source);
	if (source.MipLevels != 1 || source.ArraySize != 1 || source.SampleDesc.Count != 1 ||
	    source.Usage != D3D11_USAGE_DEFAULT || !(source.MiscFlags & D3D11_RESOURCE_MISC_SHARED) ||
	    (source.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
		throw std::runtime_error("DML bridge requires an unkeyed shared single-sample texture");
	if (source.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
		s.format = 0;
	else if (source.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
		s.format = 1;
	else if (source.Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
		s.format = 2;
	else
		throw std::runtime_error("Unsupported DML shared texture format");
	s.width = source.Width;
	s.height = source.Height;
	s.worker = worker_for(*s.context);
	s.device = s.worker->device;
	s.immediate = s.worker->immediate;
	ComPtr<IDXGIResource> legacy;
	HANDLE legacy_handle = nullptr;
	check(texture->QueryInterface(IID_PPV_ARGS(&legacy)), "Query OBS shared texture");
	check(legacy->GetSharedHandle(&legacy_handle), "Read OBS shared texture handle");
	check(s.device->OpenSharedResource(legacy_handle, IID_PPV_ARGS(&s.alias)), "Open OBS shared texture on worker");
	D3D11_TEXTURE2D_DESC bridge_desc = source;
	bridge_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	check(s.device->CreateTexture2D(&bridge_desc, nullptr, &s.bridge), "Create D3D11 NT bridge texture");
	ComPtr<IDXGIResource1> nt_resource;
	check(s.bridge.As(&nt_resource), "Query bridge NT resource");
	Handle handle;
	check(nt_resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
					      &handle.value),
	      "Create texture NT handle");
	check(s.context->device()->OpenSharedHandle(handle.value, IID_PPV_ARGS(&s.shared)),
	      "Open D3D11 bridge texture in D3D12");
	const auto resource = s.shared->GetDesc();
	check(s.context->device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&s.fence)),
	      "Create shared bridge fence");
	Handle fence_handle;
	check(s.context->device()->CreateSharedHandle(s.fence.Get(), nullptr, GENERIC_ALL, nullptr,
						      &fence_handle.value),
	      "Create fence NT handle");
	check(s.device->OpenSharedFence(fence_handle.value, IID_PPV_ARGS(&s.fence11)), "Open D3D11 shared fence");
	s.context->device()->GetCopyableFootprints(&resource, 0, 1, 0, &s.footprint, nullptr, nullptr, nullptr);
	s.event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!s.event.value)
		throw std::runtime_error("Cannot create shared texture event");
}
Texture::~Texture()
{
	if (impl_ && impl_->submitted)
		(void)impl_.release();
}
std::shared_ptr<Buffer> Texture::read()
{
	if (!impl_->linear)
		impl_->linear = impl_->context->allocate(static_cast<std::size_t>(pitch()) * height());
	impl_->transfer(*impl_->linear, false);
	return impl_->linear;
}
void Texture::write(const Buffer &buffer)
{
	impl_->transfer(buffer, true);
}
std::shared_ptr<Buffer> Texture::conversion_buffer(std::size_t bytes)
{
	if (!impl_->conversion)
		impl_->conversion = impl_->context->allocate(bytes);
	if (impl_->conversion->bytes() != bytes)
		throw std::invalid_argument("DML conversion buffer size changed");
	return impl_->conversion;
}
unsigned Texture::width() const
{
	return impl_->width;
}
unsigned Texture::height() const
{
	return impl_->height;
}
unsigned Texture::pitch() const
{
	return impl_->footprint.Footprint.RowPitch;
}
unsigned Texture::format() const
{
	return impl_->format;
}
#else
struct Texture::Impl {};
Texture::Texture(std::shared_ptr<Context>, ID3D11Texture2D *)
{
	throw std::runtime_error("DML textures require Windows");
}
Texture::~Texture() = default;
std::shared_ptr<Buffer> Texture::read()
{
	throw std::runtime_error("DML textures require Windows");
}
void Texture::write(const Buffer &)
{
	throw std::runtime_error("DML textures require Windows");
}
std::shared_ptr<Buffer> Texture::conversion_buffer(std::size_t)
{
	throw std::runtime_error("DML textures require Windows");
}
unsigned Texture::width() const
{
	return 0;
}
unsigned Texture::height() const
{
	return 0;
}
unsigned Texture::pitch() const
{
	return 0;
}
unsigned Texture::format() const
{
	return 0;
}
#endif
} // namespace corridorkey::dml
