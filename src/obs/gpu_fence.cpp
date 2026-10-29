// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "gpu_fence.hpp"
#include <graphics/graphics.h>
#include <cstdint>
#include <atomic>
#ifdef _WIN32
#include <d3d11_4.h>
#include <wrl/client.h>
#else
#include <dlfcn.h>
#endif

namespace corridorkey {
struct GpuFence::Impl {
	bool armed = false;
	mutable std::atomic<bool> failed{false};
#ifdef _WIN32
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<ID3D11Query> query;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext4> timeline_context;
	Microsoft::WRL::ComPtr<ID3D11Fence> timeline;
	HANDLE event = nullptr;
	UINT64 value = 0;
	Impl()
	{
		if (gs_get_device_type() != GS_DEVICE_DIRECT3D_11)
			return;
		auto *device = static_cast<ID3D11Device *>(gs_get_device_obj());
		if (!device)
			return;
		device->GetImmediateContext(&context);
		D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
		if (FAILED(device->CreateQuery(&desc, &query)))
			failed = true;
		Microsoft::WRL::ComPtr<ID3D11Device5> modern;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&modern))) &&
		    SUCCEEDED(context.As(&timeline_context)) &&
		    SUCCEEDED(modern->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline))))
			event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!event) {
			timeline.Reset();
			timeline_context.Reset();
		}
	}
	~Impl()
	{
		if (event)
			CloseHandle(event);
	}
	bool supported() const { return context && query && !failed; }
	bool signal()
	{
		if (!supported())
			return false;
		if (timeline) {
			ResetEvent(event);
			++value;
			failed = FAILED(timeline_context->Signal(timeline.Get(), value)) ||
				 FAILED(timeline->SetEventOnCompletion(value, event));
			armed = true;
			return !failed;
		}
		context->End(query.Get());
		armed = true;
		return true;
	}
	bool ready() const
	{
		if (!armed)
			return true;
		if (timeline) {
			const auto completed = timeline->GetCompletedValue();
			if (completed == UINT64_MAX)
				failed = true;
			return !failed && completed >= value;
		}
		BOOL complete = FALSE;
		const auto hr =
			context->GetData(query.Get(), &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (FAILED(hr))
			failed = true;
		return hr == S_OK && complete;
	}
#else
	// Resolve core GL sync functions without depending on OBS's private GL loader.
	void *library = nullptr, *sync = nullptr;
	using Fence = void *(*)(unsigned, unsigned);
	using Wait = unsigned (*)(void *, unsigned, std::uint64_t);
	using Delete = void (*)(void *);
	Fence fence = nullptr;
	Wait wait = nullptr;
	Delete destroy = nullptr;
	Impl()
	{
		if (gs_get_device_type() != GS_DEVICE_OPENGL)
			return;
		library = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
		if (!library)
			library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (library) {
			fence = reinterpret_cast<Fence>(dlsym(library, "glFenceSync"));
			wait = reinterpret_cast<Wait>(dlsym(library, "glClientWaitSync"));
			destroy = reinterpret_cast<Delete>(dlsym(library, "glDeleteSync"));
		}
	}
	~Impl()
	{
		if (sync)
			destroy(sync);
		if (library)
			dlclose(library);
	}
	bool supported() const { return fence && wait && destroy && !failed; }
	bool signal()
	{
		if (!supported())
			return false;
		if (sync)
			destroy(sync);
		sync = fence(0x9117 /* GL_SYNC_GPU_COMMANDS_COMPLETE */, 0);
		armed = true;
		failed = !sync;
		return !failed;
	}
	bool ready() const
	{
		if (!armed)
			return true;
		if (!sync)
			return false;
		const auto result = wait(sync, 0, 0);
		failed = result == 0x911D /* GL_WAIT_FAILED */;
		return result == 0x911A /* GL_ALREADY_SIGNALED */ || result == 0x911C /* GL_CONDITION_SATISFIED */;
	}
#endif
};
GpuFence::GpuFence() : impl_(std::make_unique<Impl>()) {}
GpuFence::~GpuFence() = default;
bool GpuFence::supported() const noexcept
{
	return impl_->supported();
}
bool GpuFence::signal() noexcept
{
	return impl_->signal();
}
bool GpuFence::ready() const noexcept
{
	return impl_->ready();
}
bool GpuFence::failed() const noexcept
{
	return impl_->failed;
}
bool GpuFence::can_wait() const noexcept
{
#ifdef _WIN32
	return impl_->timeline && impl_->event;
#else
	return false;
#endif
}
bool GpuFence::wait_ready(unsigned timeout_ms) const noexcept
{
#ifdef _WIN32
	if (can_wait())
		return ready() || (WaitForSingleObject(impl_->event, timeout_ms) == WAIT_OBJECT_0 && ready());
#endif
	(void)timeout_ms;
	return false;
}
} // namespace corridorkey
