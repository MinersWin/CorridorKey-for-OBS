// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <array>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#else
#include <dlfcn.h>
#endif

namespace corridorkey::ort {
namespace {
std::string luid_string(const void *data)
{
	// Numeric high:low LUID, independent of byte order in external text.
	std::array<std::uint32_t, 2> words{};
	std::memcpy(words.data(), data, 8);
	std::ostringstream out;
	out << "luid:" << std::hex << std::setfill('0') << std::setw(8) << words[1] << ':' << std::setw(8) << words[0];
	return out.str();
}
} // namespace
std::vector<Device> enumerate_devices(Backend backend)
{
	std::vector<Device> out;
	if (backend == Backend::cpu_diagnostic)
		return {Device{}};
#ifdef _WIN32
	if (backend == Backend::directml) {
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			throw std::runtime_error("DXGI enumeration failed");
		for (UINT i = 0;; ++i) {
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			const auto hr = factory->EnumAdapters1(i, &adapter);
			if (hr == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(hr))
				throw std::runtime_error("DXGI adapter query failed");
			DXGI_ADAPTER_DESC1 desc{};
			if (FAILED(adapter->GetDesc1(&desc)))
				continue;
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;
			Device d;
			d.ordinal = static_cast<int>(i);
			d.adapter.stable_id = luid_string(&desc.AdapterLuid);
			d.adapter.vendor_id = desc.VendorId;
			d.adapter.device_id = desc.DeviceId;
			char name[512]{};
			WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
			d.adapter.display_name = name;
			LARGE_INTEGER driver{};
			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
				d.driver = std::to_string(driver.QuadPart);
			out.push_back(std::move(d));
		}
		return out;
	}
#endif
	if (backend != Backend::cuda && backend != Backend::tensorrt && backend != Backend::tensorrt_rtx)
		return out;
#ifdef _WIN32
	const auto library = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	auto symbol = [&](const char *name) {
		return library ? reinterpret_cast<void *>(GetProcAddress(library, name)) : nullptr;
	};
#else
	const auto library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
	auto symbol = [&](const char *name) {
		return library ? dlsym(library, name) : nullptr;
	};
#endif
	// The system driver stays resident; no CUDA context or allocation is created here.
	auto init = reinterpret_cast<int (*)(unsigned)>(symbol("cuInit"));
	auto count = reinterpret_cast<int (*)(int *)>(symbol("cuDeviceGetCount"));
	auto get = reinterpret_cast<int (*)(int *, int)>(symbol("cuDeviceGet"));
	auto name = reinterpret_cast<int (*)(char *, int, int)>(symbol("cuDeviceGetName"));
	auto pci = reinterpret_cast<int (*)(char *, int, int)>(symbol("cuDeviceGetPCIBusId"));
	auto version = reinterpret_cast<int (*)(int *)>(symbol("cuDriverGetVersion"));
	auto compute = reinterpret_cast<int (*)(int *, int *, int)>(symbol("cuDeviceComputeCapability"));
#ifdef _WIN32
	auto luid = reinterpret_cast<int (*)(char *, unsigned *, int)>(symbol("cuDeviceGetLuid"));
#endif
	if (!init || !count || !get || !name || !pci || !version || !compute || init(0))
		return out;
	int total = 0, driver = 0;
	if (count(&total) || version(&driver))
		return out;
	for (int i = 0; i < total; ++i) {
		int id = 0, major = 0, minor = 0;
		char label[256]{}, bus[64]{};
		if (get(&id, i) || name(label, sizeof(label), id) || pci(bus, sizeof(bus), id) ||
		    compute(&major, &minor, id))
			continue;
		Device d;
		d.ordinal = i;
		d.adapter.vendor_id = 0x10de;
		d.adapter.display_name = label;
		d.driver = std::to_string(driver);
		d.architecture = "sm_" + std::to_string(major) + std::to_string(minor);
		d.pci_id = "pci:" + std::string(bus);
		d.adapter.stable_id = d.pci_id;
#ifdef _WIN32
		char bytes[8]{};
		unsigned mask = 0;
		if (luid && !luid(bytes, &mask, id))
			d.adapter.stable_id = luid_string(bytes);
		// CUDA's API version alone does not identify a driver update.
		for (const auto &dxgi : enumerate_devices(Backend::directml)) {
			if (adapter_matches(d.adapter, dxgi.adapter)) {
				d.driver = dxgi.driver + "/cuda-" + d.driver;
				d.adapter.device_id = dxgi.adapter.device_id;
				break;
			}
		}
#else
		std::ifstream driver_file("/proc/driver/nvidia/version");
		std::string driver_text;
		std::getline(driver_file, driver_text);
		d.driver = driver_text.empty() ? "" : driver_text + "/cuda-" + d.driver;
#endif
		out.push_back(std::move(d));
	}
	return out;
}
} // namespace corridorkey::ort
