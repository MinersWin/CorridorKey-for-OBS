// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine/providers/directml.hpp"
#include <map>
struct ID3D11Texture2D;
namespace corridorkey::dml {
class Texture {
public:
	Texture(std::shared_ptr<Context>, ID3D11Texture2D *);
	~Texture();
	Texture(const Texture &) = delete;
	Texture &operator=(const Texture &) = delete;
	std::shared_ptr<Buffer> read();
	void write(const Buffer &);
	std::shared_ptr<Buffer> conversion_buffer(std::size_t);
	unsigned width() const;
	unsigned height() const;
	unsigned pitch() const;
	unsigned format() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
class TextureTransfers {
public:
	Texture &get(std::shared_ptr<Context>, ID3D11Texture2D *);
	void clear() { textures_.clear(); }

private:
	std::map<std::pair<Context *, ID3D11Texture2D *>, std::unique_ptr<Texture>> textures_;
};
} // namespace corridorkey::dml
