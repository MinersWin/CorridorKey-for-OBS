// SPDX-License-Identifier: GPL-3.0-or-later
#include "d3d11_cuda.hpp"
#include <stdexcept>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstring>
#endif
namespace corridorkey::nvidia {
D3D11Texture &TextureTransfers::get(std::shared_ptr<Context> context, ID3D11Texture2D *texture)
{
	auto found = textures_.find(texture);
	if (found != textures_.end())
		return *found->second;
	if (textures_.size() >= 32)
		textures_.clear();
	auto entry = std::make_unique<D3D11Texture>(std::move(context), texture);
	auto &result = *entry;
	textures_.emplace(texture, std::move(entry));
	return result;
}
#ifdef _WIN32
namespace {
// Serialize cross-API ownership transitions across the input and refiner workers.
// Interleaving maps on the legacy stream can introduce circular graphics/CUDA waits.
std::mutex interop_mutex;
struct InteropDevice {
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	explicit InteropDevice(IDXGIAdapter *adapter)
	{
		if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
					     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
					     nullptr, nullptr)))
			throw std::runtime_error("Cannot create worker D3D11 device");
	}
};
std::map<std::pair<LONG, DWORD>, std::weak_ptr<InteropDevice>> worker_devices;
// CUDA_MEMCPY2D v2 ABI; driver types are declared locally to keep CUDA optional at build time.
struct Copy2D {
	std::size_t src_x = 0, src_y = 0;
	unsigned src_type = 0;
	const void *src_host = nullptr;
	Pointer src_device = 0;
	void *src_array = nullptr;
	std::size_t src_pitch = 0, dst_x = 0, dst_y = 0;
	unsigned dst_type = 0;
	void *dst_host = nullptr;
	Pointer dst_device = 0;
	void *dst_array = nullptr;
	std::size_t dst_pitch = 0, width = 0, height = 0;
};
template<typename T> T load(const Context &context, const char *name)
{
	auto function = reinterpret_cast<T>(context.symbol(name));
	if (!function)
		throw std::runtime_error("CUDA driver lacks D3D11 interop API");
	return function;
}
} // namespace
struct D3D11Texture::Impl {
	std::shared_ptr<Context> context;
	std::shared_ptr<InteropDevice> device;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> original, alias;
	std::shared_ptr<Buffer> linear;
	void *resource = nullptr;
	unsigned width = 0, height = 0, format = 0, pixel_bytes = 0;
	bool mapped = false;
	int(__stdcall *unregister)(void *) = nullptr;
	int(__stdcall *map)(unsigned, void **, void *) = nullptr;
	int(__stdcall *unmap)(unsigned, void **, void *) = nullptr;
	int(__stdcall *array)(void **, void *, unsigned, unsigned) = nullptr;
	int(__stdcall *copy)(const Copy2D *) = nullptr;
	int(__stdcall *sync)(void *) = nullptr;
	~Impl()
	{
		if (!resource)
			return;
		try {
			std::lock_guard lock(interop_mutex);
			Context::Scope scope(*context);
			if (mapped)
				unmap(1, &resource, nullptr);
			unregister(resource);
		} catch (...) {
		}
	}
	void transfer(const Buffer &buffer, bool upload)
	{
		if (buffer.bytes() != static_cast<std::size_t>(width) * height * pixel_bytes ||
		    buffer.context()->ordinal() != context->ordinal())
			throw std::invalid_argument("Interop buffer mismatch");
		std::lock_guard lock(interop_mutex);
		Context::Scope scope(*context);
		Context::check(map(1, &resource, nullptr), "Map D3D11 resource");
		mapped = true;
		try {
			void *mapped_array = nullptr;
			Context::check(array(&mapped_array, resource, 0, 0), "Get mapped texture array");
			Copy2D region;
			region.width = region.src_pitch = region.dst_pitch =
				static_cast<std::size_t>(width) * pixel_bytes;
			region.height = height;
			region.src_type = upload ? 2U : 3U;
			region.dst_type = upload ? 3U : 2U;
			if (upload) {
				region.src_device = buffer.pointer();
				region.dst_array = mapped_array;
			} else {
				region.src_array = mapped_array;
				region.dst_device = buffer.pointer();
			}
			Context::check(copy(&region), "Copy mapped texture");
		} catch (...) {
			if (unmap(1, &resource, nullptr) == 0)
				mapped = false;
			throw;
		}
		Context::check(unmap(1, &resource, nullptr), "Unmap D3D11 resource");
		mapped = false;
		// The consuming graphics device is different from the registration device.
		Context::check(sync(nullptr), "Complete shared texture transfer");
	}
};
D3D11Texture::D3D11Texture(std::shared_ptr<Context> context, ID3D11Texture2D *texture) : impl_(std::make_unique<Impl>())
{
	if (!texture)
		throw std::invalid_argument("Missing D3D11 texture");
	auto &s = *impl_;
	s.context = std::move(context);
	std::lock_guard lock(interop_mutex);
	Context::Scope scope(*s.context);
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	texture->GetDevice(&device);
	Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC adapter_desc{};
	if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&adapter_desc)))
		throw std::runtime_error("Cannot identify D3D11 adapter");
	auto get_device = load<int(__stdcall *)(int *, IDXGIAdapter *)>(*s.context, "cuD3D11GetDevice");
	auto get_luid = load<int(__stdcall *)(char *, unsigned *, int)>(*s.context, "cuDeviceGetLuid");
	int ordinal = -1;
	char luid[8]{};
	unsigned node = 0;
	Context::check(get_device(&ordinal, adapter.Get()), "Match D3D11 CUDA adapter");
	Context::check(get_luid(luid, &node, ordinal), "Read CUDA adapter LUID");
	if (ordinal != s.context->ordinal() || std::memcmp(luid, &adapter_desc.AdapterLuid, 8) != 0)
		throw std::runtime_error("OBS and inference CUDA adapters do not match");
	const auto key = std::pair{adapter_desc.AdapterLuid.HighPart, adapter_desc.AdapterLuid.LowPart};
	s.device = worker_devices[key].lock();
	if (!s.device) {
		s.device = std::make_shared<InteropDevice>(adapter.Get());
		worker_devices[key] = s.device;
	}
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	if (desc.MipLevels != 1 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
	    desc.Usage != D3D11_USAGE_DEFAULT)
		throw std::runtime_error("D3D11 texture is not eligible for CUDA registration");
	if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED))
		throw std::runtime_error("CUDA transport requires a shared D3D11 texture");
	Microsoft::WRL::ComPtr<IDXGIResource> shared;
	HANDLE handle = nullptr;
	if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&shared))) || FAILED(shared->GetSharedHandle(&handle)) ||
	    FAILED(s.device->device->OpenSharedResource(handle, IID_PPV_ARGS(&s.alias))))
		throw std::runtime_error("Cannot open texture on worker D3D11 device");
	s.original = texture;
	switch (desc.Format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		s.format = 0;
		s.pixel_bytes = 4;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		s.format = 1;
		s.pixel_bytes = 4;
		break;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		s.format = 2;
		s.pixel_bytes = 16;
		break;
	default:
		throw std::runtime_error("Unsupported D3D11 interop format");
	}
	s.width = desc.Width;
	s.height = desc.Height;
	s.unregister = load<decltype(s.unregister)>(*s.context, "cuGraphicsUnregisterResource");
	s.map = load<decltype(s.map)>(*s.context, "cuGraphicsMapResources");
	s.unmap = load<decltype(s.unmap)>(*s.context, "cuGraphicsUnmapResources");
	s.array = load<decltype(s.array)>(*s.context, "cuGraphicsSubResourceGetMappedArray");
	s.copy = load<decltype(s.copy)>(*s.context, "cuMemcpy2D_v2");
	s.sync = load<decltype(s.sync)>(*s.context, "cuStreamSynchronize");
	auto reg = load<int(__stdcall *)(void **, ID3D11Resource *, unsigned)>(*s.context,
									       "cuGraphicsD3D11RegisterResource");
	Context::check(reg(&s.resource, s.alias.Get(), 0), "Register shared D3D11 texture");
}
D3D11Texture::~D3D11Texture() = default;
std::shared_ptr<Buffer> D3D11Texture::read()
{
	if (!impl_->linear)
		impl_->linear = impl_->context->allocate(static_cast<std::size_t>(impl_->width) * impl_->height *
							 impl_->pixel_bytes);
	impl_->transfer(*impl_->linear, false);
	return impl_->linear;
}
void D3D11Texture::write(const Buffer &buffer)
{
	impl_->transfer(buffer, true);
}
unsigned D3D11Texture::width() const
{
	return impl_->width;
}
unsigned D3D11Texture::height() const
{
	return impl_->height;
}
unsigned D3D11Texture::format() const
{
	return impl_->format;
}
#else
struct D3D11Texture::Impl {};
D3D11Texture::D3D11Texture(std::shared_ptr<Context>, ID3D11Texture2D *)
{
	throw std::runtime_error("D3D11 requires Windows");
}
D3D11Texture::~D3D11Texture() = default;
std::shared_ptr<Buffer> D3D11Texture::read()
{
	throw std::runtime_error("D3D11 requires Windows");
}
void D3D11Texture::write(const Buffer &)
{
	throw std::runtime_error("D3D11 requires Windows");
}
unsigned D3D11Texture::width() const
{
	return 0;
}
unsigned D3D11Texture::height() const
{
	return 0;
}
unsigned D3D11Texture::format() const
{
	return 0;
}
#endif
} // namespace corridorkey::nvidia
