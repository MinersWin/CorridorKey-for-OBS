// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "contracts/settings.hpp"
#include <array>
#include <vector>
namespace post_reference {
using Pixel = std::array<double, 4>;
struct Image {
	unsigned width, height;
	std::vector<Pixel> pixels;
	Image(unsigned w, unsigned h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h) {}
	Pixel &at(unsigned x, unsigned y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
	const Pixel &at(unsigned x, unsigned y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
};
struct Fields {
	Image coefficients, delta, background, band, raw, output;
};
Fields reconstruct(const Image &native, const Image &guide, const Image &model, const corridorkey::Settings &settings,
		   const corridorkey::Crop &roi);
double decode(double x);
double encode(double x);
} // namespace post_reference
