// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine/providers/nvidia.hpp"
#include <map>
struct ID3D11Texture2D;
namespace corridorkey::nvidia {
// The owner keeps the graphics slot pinned and prevents D3D access during each call.
// Registration, mapping, conversion and teardown belong to a worker, never video_render.
class D3D11Texture {
public:
	D3D11Texture(std::shared_ptr<Context>, ID3D11Texture2D *);
	~D3D11Texture();
	D3D11Texture(const D3D11Texture &) = delete;
	D3D11Texture &operator=(const D3D11Texture &) = delete;
	std::shared_ptr<Buffer> read();
	void write(const Buffer &);
	unsigned width() const;
	unsigned height() const;
	unsigned format() const; // 0: RGBA8, 1: BGRA8, 2: RGBA32F
private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// Owned and destroyed by the inference coordinator worker. Registrations retain the COM textures.
class TextureTransfers {
public:
	D3D11Texture &get(std::shared_ptr<Context>, ID3D11Texture2D *);
	void clear() { textures_.clear(); }

private:
	std::map<ID3D11Texture2D *, std::unique_ptr<D3D11Texture>> textures_;
};
} // namespace corridorkey::nvidia
