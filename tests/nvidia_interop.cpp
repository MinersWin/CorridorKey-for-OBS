// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu/d3d11_cuda.hpp"
#include "engine/ck2_graph.hpp"
#include "engine/refiner_stage.hpp"
#define NOMINMAX
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace corridorkey;
using Microsoft::WRL::ComPtr;
void require(bool ok, const char *message)
{
	if (!ok)
		throw std::runtime_error(message);
}
void check(HRESULT hr)
{
	require(SUCCEEDED(hr), "D3D11 operation failed");
}
int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> graphics;
		check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
					&device, nullptr, &graphics));
		auto cuda = std::make_shared<nvidia::Context>(0);
		for (auto size :
		     {Extent{1, 1}, Extent{37, 19}, Extent{512, 256}, Extent{1920, 1080}, Extent{3840, 2160}}) {
			const auto pixels = static_cast<std::size_t>(size.width) * size.height;
			std::vector<unsigned char> bytes(pixels * 4);
			for (std::size_t i = 0; i < bytes.size(); ++i)
				bytes[i] = static_cast<unsigned char>((i * 13 + i / 17) % 256);
			for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = size.width;
				desc.Height = size.height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = format;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
				D3D11_SUBRESOURCE_DATA initial{bytes.data(), size.width * 4, 0};
				ComPtr<ID3D11Texture2D> input, output, stage;
				check(device->CreateTexture2D(&desc, &initial, &input));
				desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				check(device->CreateTexture2D(&desc, nullptr, &output));
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				check(device->CreateTexture2D(&desc, nullptr, &stage));
				graphics->Flush();
				nvidia::D3D11Texture source(cuda, input.Get()), destination(cuda, output.Get());
				auto rgb = ck2::device_rgb(source.read(), size, source.format());
				auto host = ck2::download_tensor(rgb);
				float maximum = 0;
				for (std::size_t p = 0; p < pixels; ++p)
					for (unsigned c = 0; c < 3; ++c) {
						unsigned channel = format == DXGI_FORMAT_B8G8R8A8_UNORM ? 2 - c : c;
						maximum = std::max(maximum, std::abs(host.values[c * pixels + p] -
										     bytes[p * 4 + channel] / 255.f));
					}
				require(maximum < 0.000001f, "Texture channel conversion failed");
				auto alpha = cuda->allocate(pixels * sizeof(float));
				std::vector<float> a(pixels, .37f);
				alpha->upload(a.data(), a.size() * sizeof(float));
				auto packed = cuda->allocate(pixels * 4 * sizeof(float));
				auto f = rgb.device->pointer(), ap = alpha->pointer(), dst = packed->pointer();
				auto count = static_cast<unsigned>(pixels);
				void *args[]{&f, &ap, &dst, &count};
				cuda->launch("planar_to_rgba", count * 4, args);
				cuda->synchronize();
				destination.write(*packed);
				graphics->CopyResource(stage.Get(), output.Get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				check(graphics->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped));
				bool valid = true;
				for (unsigned y = 0; y < size.height; ++y) {
					const auto *row = reinterpret_cast<const float *>(
						static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch);
					for (unsigned x = 0; x < size.width; ++x)
						for (unsigned c = 0; c < 4; ++c)
							valid = valid &&
								std::abs(row[x * 4 + c] -
									 (c == 3 ? .37f
										 : host.values[c * pixels +
											       y * size.width + x])) <
									0.000001f;
				}
				graphics->Unmap(stage.Get(), 0);
				require(valid, "Texture write/alpha round trip failed");
				const auto reduced = ck2::download_tensor(ck2::resize_area(rgb, {31, 17}));
				const auto oracle = ck2::resize_area(host, {31, 17});
				for (std::size_t i = 0; i < reduced.values.size(); ++i)
					require(std::abs(reduced.values[i] - oracle.values[i]) < 0.000001f,
						"Area resize differs from CPU reference");
				std::cout << "roundtrip " << size.width << 'x' << size.height << " format=" << format
					  << " max=" << maximum << '\n';
			}
		}
		for (unsigned budget : {256U, 384U, 512U, 768U}) {
			auto size = refiner::input_atlas(budget);
			std::vector<float> atlas(static_cast<std::size_t>(size.width) * size.height * 4);
			for (std::size_t i = 0; i < atlas.size(); ++i)
				atlas[i] = static_cast<float>((i * 37) % 65537) / 65536.f;
			auto oracle = refiner::unpack_atlas(atlas, budget);
			auto input = cuda->allocate(atlas.size() * sizeof(float));
			input->upload(atlas.data(), input->bytes());
			auto output = cuda->allocate(oracle.values.size() * sizeof(float));
			auto src = input->pointer(), dst = output->pointer();
			void *args[]{&src, &dst, &budget};
			cuda->launch("unpack_atlas", static_cast<unsigned>(oracle.values.size()), args);
			std::vector<float> actual(oracle.values.size());
			output->download(actual.data(), output->bytes());
			require(actual == oracle.values, "CUDA atlas unpack differs from production CPU reference");
			std::cout << "atlas b" << budget << " exact\n";
		}
		auto invalid = cuda->allocate(4);
		float nan = std::nanf("");
		invalid->upload(&nan, 4);
		bool rejected = false;
		try {
			cuda->validate(*invalid, false);
		} catch (...) {
			rejected = true;
		}
		require(rejected, "Non-finite CUDA tensor accepted");
		std::cout << "PASS\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
