// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ck2_graph.hpp"
#include "engine/refiner_stage.hpp"
#include "engine/providers/runtime.hpp"
#include "gpu/d3d12_dml.hpp"
#define NOMINMAX
#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace corridorkey;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool ok, const char *message)
{
	if (!ok)
		throw std::runtime_error(message);
}
void flush(ID3D11Device *device, ID3D11DeviceContext *immediate)
{
	ComPtr<ID3D11Query> query;
	D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
	require(SUCCEEDED(device->CreateQuery(&desc, &query)), "Create test fence");
	immediate->End(query.Get());
	immediate->Flush();
	const auto start = GetTickCount64();
	while (immediate->GetData(query.Get(), nullptr, 0, 0) == S_FALSE) {
		require(GetTickCount64() - start < 10000, "Test input fence timed out");
		Sleep(1);
	}
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device *device, unsigned w, unsigned h, DXGI_FORMAT format, const void *pixels,
				unsigned pitch)
{
	D3D11_TEXTURE2D_DESC d{};
	d.Width = w;
	d.Height = h;
	d.MipLevels = d.ArraySize = 1;
	d.Format = format;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	D3D11_SUBRESOURCE_DATA data{pixels, pitch, 0};
	ComPtr<ID3D11Texture2D> result;
	require(SUCCEEDED(device->CreateTexture2D(&d, pixels ? &data : nullptr, &result)),
		"Create test shared texture");
	return result;
}
void compare(const std::vector<float> &a, const std::vector<float> &b, float tolerance)
{
	require(a.size() == b.size(), "Comparison size mismatch");
	for (std::size_t i = 0; i < a.size(); ++i)
		if (!(std::isfinite(a[i]) && std::abs(a[i] - b[i]) <= tolerance)) {
			std::cerr << "Mismatch at " << i << ": " << a[i] << " vs " << b[i] << " tolerance " << tolerance
				  << '\n';
			throw std::runtime_error("DML conversion parity failure");
		}
}
} // namespace
int main(int argc, char **argv)
{
	try {
		if (argc != 2)
			throw std::runtime_error("Usage: corridorkey_dml_interop_test <absolute DirectML.dll>");
		const auto devices = ort::enumerate_devices(Backend::directml);
		require(!devices.empty(), "No DirectML adapter");
		auto context = std::make_shared<dml::Context>(devices.front().adapter.stable_id,
							      std::filesystem::path(argv[1]));
		context->probe();
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> immediate;
		require(SUCCEEDED(D3D11CreateDevice(context->adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
						    D3D11_SDK_VERSION, &device, nullptr, &immediate)),
			"Create test D3D11 device");
		for (unsigned width : {31U, 512U, 513U}) {
			const unsigned height = 17;
			std::vector<unsigned char> pixels(width * height * 4);
			for (std::size_t i = 0; i < pixels.size(); ++i)
				pixels[i] = static_cast<unsigned char>(i * 37);
			auto original = texture(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels.data(),
						width * 4);
			flush(device.Get(), immediate.Get());
			dml::Texture shared(context, original.Get());
			auto raw = shared.read();
			auto planar = context->allocate(width * height * 3 * 4);
			const unsigned params[]{width, height, 0, 0, 0, shared.pitch(), shared.format()};
			context->compute("rgba_to_planar", width * height * 3, *raw, nullptr, *planar, params);
			std::vector<float> actual(width * height * 3), expected(actual.size());
			planar->download(actual.data(), actual.size() * 4);
			for (unsigned p = 0; p < width * height; ++p)
				for (unsigned c = 0; c < 3; ++c)
					expected[c * width * height + p] = pixels[p * 4 + c] / 255.f;
			compare(actual, expected, 1e-7f);
			context->validate(*planar, true);
			ck2::Tensor cpu({1, 3, height, width});
			cpu.values = expected;
			ck2::Tensor gpu({1, 3, height, width}, 0, false);
			gpu.directml = planar;
			auto resized = ck2::download_tensor(ck2::resize_area(gpu, {11, 7}));
			compare(resized.values, ck2::resize_area(cpu, {11, 7}).values, 1e-6f);
			std::cout << "RGBA8 stride/resize " << width << " passed\n";
		}
		for (unsigned budget : {256U, 384U, 512U, 768U}) {
			const auto size = refiner::input_atlas(budget);
			std::vector<float> pixels(static_cast<std::size_t>(size.width) * size.height * 4);
			for (std::size_t i = 0; i < pixels.size(); ++i)
				pixels[i] = static_cast<float>(i % 65537) / 65536;
			auto original = texture(device.Get(), size.width, size.height, DXGI_FORMAT_R32G32B32A32_FLOAT,
						pixels.data(), size.width * 16);
			flush(device.Get(), immediate.Get());
			dml::Texture shared(context, original.Get());
			auto raw = shared.read();
			auto expected = refiner::unpack_atlas(pixels, budget);
			auto result = context->allocate(expected.values.size() * 4);
			const unsigned params[]{budget, 0, 0, 0, 0, shared.pitch(), 0};
			context->compute("unpack_atlas", static_cast<unsigned>(expected.values.size()), *raw, nullptr,
					 *result, params);
			std::vector<float> actual(expected.values.size());
			result->download(actual.data(), actual.size() * 4);
			compare(actual, expected.values, 0);
			auto tensors = ck2::output_contract(ck2::Stage::refiner, {budget, 52});
			for (auto &[name, t] : tensors) {
				for (std::size_t i = 0; i < t.values.size(); ++i)
					t.values[i] = static_cast<float>(i % 101) / 100;
				t.directml = context->allocate(t.values.size() * 4);
				t.directml->upload(t.values.data(), t.values.size() * 4);
			}
			auto centres = refiner::pack_centres(tensors, budget);
			auto packed = context->allocate(centres.size() * 4);
			const unsigned pack_params[]{budget, 0, 0, 0, 0, 0, 0};
			context->compute("pack_centres", static_cast<unsigned>(centres.size()),
					 *tensors.at("fg").directml, tensors.at("alpha").directml.get(), *packed,
					 pack_params);
			std::vector<float> actual_centres(centres.size());
			packed->download(actual_centres.data(), actual_centres.size() * 4);
			compare(actual_centres, centres, 0);
			std::cout << "Refiner atlas/centres " << budget << " exact parity\n";
		}
		const unsigned width = 256, height = 13;
		std::vector<float> rgba(width * height * 4);
		for (std::size_t i = 0; i < rgba.size(); ++i)
			rgba[i] = static_cast<float>(i % 101) / 100;
		auto original = texture(device.Get(), width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0);
		dml::Texture output(context, original.Get());
		auto buffer = context->allocate(rgba.size() * 4);
		buffer->upload(rgba.data(), rgba.size() * 4);
		output.write(*buffer);
		D3D11_TEXTURE2D_DESC desc{};
		original->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "Create test readback");
		immediate->CopyResource(staging.Get(), original.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		require(SUCCEEDED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
			"Read published shared output");
		std::vector<float> actual(rgba.size());
		for (unsigned y = 0; y < height; ++y)
			std::memcpy(actual.data() + y * width * 4,
				    static_cast<const char *>(mapped.pData) + y * mapped.RowPitch, width * 16);
		immediate->Unmap(staging.Get(), 0);
		compare(actual, rgba, 0);
		std::vector<float> invalid(4, 0);
		invalid[2] = std::numeric_limits<float>::quiet_NaN();
		auto bad = context->allocate(16);
		bad->upload(invalid.data(), 16);
		bool rejected = false;
		try {
			context->validate(*bad, false);
		} catch (const std::runtime_error &) {
			rejected = true;
		}
		require(rejected, "Nonfinite tensor was accepted");
		invalid = {-1, 0, 0, 0};
		bad->upload(invalid.data(), 16);
		auto unpacked = context->allocate(4);
		const unsigned params[]{16, 0, 0, 0, 0, 832 * 16, 0};
		context->compute("unpack_atlas", 1, *bad, nullptr, *unpacked, params);
		rejected = false;
		try {
			context->validate(*unpacked, false);
		} catch (const std::runtime_error &) {
			rejected = true;
		}
		require(rejected, "Out-of-range atlas input was accepted");
		std::cout << "Shared output, nonfinite and atlas range rejection passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
