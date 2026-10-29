// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/settings.hpp"
#include <graphics/graphics.h>
#include <vector>
#include <array>
#include <cstring>
#include <memory>

namespace corridorkey {
// All calls require graphics ownership. Resources are allocated before capture.
// The raw texture is straight sRGB + linear alpha, before levels/despill/source alpha.
class Reconstruction {
public:
	// Grid is the decoded model grid. Serial grid variants may share the native scratch target.
	Reconstruction(Extent native, Extent grid, const char *effect_path, Reconstruction *share_native = nullptr);
	~Reconstruction();
	Reconstruction(const Reconstruction &) = delete;
	Reconstruction &operator=(const Reconstruction &) = delete;
	bool available() const noexcept { return valid_; }
	bool prepare(gs_texture_t *native, gs_texture_t *guide, gs_texture_t *model, const Settings &settings,
		     const Crop &roi);
	bool finish(gs_texrender_t *output, gs_texture_t *native, const Settings &settings, const Crop &roi,
		    int debug_view = 0, gs_texture_t *refined = nullptr);
	void present(gs_texture_t *output);
	gs_texture_t *raw() const;
	gs_texture_t *coefficients() const;
	gs_texture_t *delta() const;
	gs_texture_t *background() const;
	gs_texture_t *band() const;
	gs_texture_t *reduction() const;

private:
	gs_eparam_t *parameter(const char *name) const;
	struct Value {
		gs_eparam_t *parameter;
		std::array<std::byte, 16> bytes;
		std::size_t size;
	};
	std::vector<Value> values_;
	std::vector<std::pair<gs_eparam_t *, gs_texture_t *>> textures_;
	template<typename T> void set(const char *name, const T &value)
	{
		static_assert(sizeof(T) <= 16);
		auto *p = parameter(name);
		for (auto &entry : values_)
			if (entry.parameter == p) {
				std::memcpy(entry.bytes.data(), &value, sizeof(T));
				entry.size = sizeof(T);
				return;
			}
		Value entry{p, {}, sizeof(T)};
		std::memcpy(entry.bytes.data(), &value, sizeof(T));
		values_.push_back(entry);
	}
	void bind();
	void texture(const char *name, gs_texture_t *value);
	void size(Extent value);
	void crop(const char *name, const Crop &value);
	bool draw(gs_texrender_t *target, Extent extent, const char *technique);
	gs_texrender_t *allocate(Extent extent);
	Extent native_, grid_;
	gs_effect_t *effect_ = nullptr;
	gs_texrender_t *solve_ = nullptr, *coefficients_ = nullptr, *delta_ = nullptr;
	gs_texrender_t *band_raw_ = nullptr, *band_ = nullptr;
	std::shared_ptr<gs_texrender_t> raw_;
	gs_texrender_t *reduction_ = nullptr;
	gs_texrender_t *background_ = nullptr;
	std::vector<gs_texrender_t *> pull_, push_, owned_;
	std::vector<Extent> levels_;
	bool valid_ = false;
};
} // namespace corridorkey
