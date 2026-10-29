// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "post_reference.hpp"
#include <algorithm>
#include <cmath>
namespace post_reference {
namespace {
double clean(double x)
{
	return std::isfinite(x) && std::abs(x) <= 65504 ? x : 0;
}
double unit(double x)
{
	return std::clamp(clean(x), 0.0, 1.0);
}
Image box(const Image &image, int radius)
{
	Image result(image.width, image.height);
	for (unsigned y = 0; y < image.height; ++y)
		for (unsigned x = 0; x < image.width; ++x) {
			int count = 0;
			for (int iy = std::max(0, static_cast<int>(y) - radius);
			     iy <= std::min(static_cast<int>(image.height) - 1, static_cast<int>(y) + radius); ++iy)
				for (int ix = std::max(0, static_cast<int>(x) - radius);
				     ix <= std::min(static_cast<int>(image.width) - 1, static_cast<int>(x) + radius);
				     ++ix) {
					for (unsigned c = 0; c < 4; ++c)
						result.at(x, y)[c] += image.at(ix, iy)[c];
					++count;
				}
			for (auto &v : result.at(x, y))
				v /= count;
		}
	return result;
}
Pixel sample(const Image &image, double u, double v)
{
	const double x = std::clamp(u * image.width - 0.5, 0.0, static_cast<double>(image.width - 1));
	const double y = std::clamp(v * image.height - 0.5, 0.0, static_cast<double>(image.height - 1));
	const auto x0 = static_cast<unsigned>(x), y0 = static_cast<unsigned>(y);
	const auto x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
	Pixel result{};
	for (unsigned c = 0; c < 4; ++c)
		result[c] = std::lerp(std::lerp(image.at(x0, y0)[c], image.at(x1, y0)[c], x - x0),
				      std::lerp(image.at(x0, y1)[c], image.at(x1, y1)[c], x - x0), y - y0);
	return result;
}
bool inside(double u, double v, const corridorkey::Crop &r)
{
	return u >= r.left && u <= r.right && v >= r.top && v <= r.bottom;
}
} // namespace
double decode(double x)
{
	x = unit(x);
	return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
double encode(double x)
{
	x = unit(x);
	return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}
Fields reconstruct(const Image &native, const Image &input_guide, const Image &input_model,
		   const corridorkey::Settings &settings, const corridorkey::Crop &roi)
{
	Image guide = input_guide, model = input_model;
	for (auto &p : guide.pixels)
		for (auto &v : p)
			v = unit(v);
	for (auto &p : model.pixels)
		for (auto &v : p)
			v = unit(v);
	const auto &q = settings.quality;
	const int radius = static_cast<int>(std::clamp(q.guided_radius, 1U, 8U));
	const auto w = guide.width, h = guide.height;
	Image joint(w, h), delta(w, h), band(w, h), seed(w, h), coefficients(w, h);
	for (std::size_t i = 0; i < joint.pixels.size(); ++i) {
		joint.pixels[i] = guide.pixels[i];
		joint.pixels[i][3] = model.pixels[i][3];
		for (unsigned c = 0; c < 3; ++c)
			delta.pixels[i][c] = model.pixels[i][c] - guide.pixels[i][c];
		const auto weight = std::pow(1 - model.pixels[i][3], 4);
		for (unsigned c = 0; c < 3; ++c)
			seed.pixels[i][c] = decode(guide.pixels[i][c]) * weight;
		seed.pixels[i][3] = weight;
	}
	const auto mean = box(joint, radius);
	// Central moments and pivoted Gaussian elimination are independent of the shader's adjugate solve.
	for (unsigned y = 0; y < h; ++y)
		for (unsigned x = 0; x < w; ++x) {
			double matrix[3][4]{};
			unsigned count = 0;
			const auto &m = mean.at(x, y);
			for (int iy = std::max(0, static_cast<int>(y) - radius);
			     iy <= std::min(static_cast<int>(h) - 1, static_cast<int>(y) + radius); ++iy)
				for (int ix = std::max(0, static_cast<int>(x) - radius);
				     ix <= std::min(static_cast<int>(w) - 1, static_cast<int>(x) + radius); ++ix) {
					const auto &p = joint.at(ix, iy);
					for (unsigned r = 0; r < 3; ++r)
						for (unsigned c = 0; c < 4; ++c)
							matrix[r][c] += (p[r] - m[r]) * (p[c] - m[c]);
					++count;
				}
			for (unsigned r = 0; r < 3; ++r) {
				for (auto &v : matrix[r])
					v /= count;
				matrix[r][r] += q.guided_epsilon;
			}
			for (unsigned k = 0; k < 3; ++k) {
				unsigned pivot = k;
				for (unsigned r = k + 1; r < 3; ++r)
					if (std::abs(matrix[r][k]) > std::abs(matrix[pivot][k]))
						pivot = r;
				for (unsigned c = 0; c < 4; ++c)
					std::swap(matrix[k][c], matrix[pivot][c]);
				const double divisor = matrix[k][k];
				for (unsigned c = k; c < 4; ++c)
					matrix[k][c] /= divisor;
				for (unsigned r = 0; r < 3; ++r)
					if (r != k) {
						const auto factor = matrix[r][k];
						for (unsigned c = k; c < 4; ++c)
							matrix[r][c] -= factor * matrix[k][c];
					}
			}
			auto &p = coefficients.at(x, y);
			p[3] = m[3];
			for (unsigned c = 0; c < 3; ++c) {
				p[c] = matrix[c][3];
				p[3] -= p[c] * m[c];
			}
			double nearby = 0;
			bool uncertain = false;
			for (int iy = std::max(0, static_cast<int>(y) - 2);
			     iy <= std::min(static_cast<int>(h) - 1, static_cast<int>(y) + 2); ++iy)
				for (int ix = std::max(0, static_cast<int>(x) - 2);
				     ix <= std::min(static_cast<int>(w) - 1, static_cast<int>(x) + 2); ++ix) {
					const auto a = model.at(ix, iy)[3];
					nearby = std::max(nearby, a);
					uncertain = uncertain || (a > q.refiner_band_lo && a < 1 - q.refiner_band_lo);
				}
			const double weight =
				uncertain ? (q.refiner_band_gain > 0 ? unit(nearby * q.refiner_band_gain) : 1) : 0;
			band.at(x, y) = {weight, weight, weight, 1};
		}
	coefficients = box(coefficients, radius);
	band = box(band, 1);
	std::vector<Image> levels{seed};
	while (levels.back().width > 1 || levels.back().height > 1) {
		const auto &previous = levels.back();
		Image next((previous.width + 1) / 2, (previous.height + 1) / 2);
		for (unsigned y = 0; y < next.height; ++y)
			for (unsigned x = 0; x < next.width; ++x) {
				unsigned count = 0;
				for (unsigned dy = 0; dy < 2; ++dy)
					for (unsigned dx = 0; dx < 2; ++dx)
						if (x * 2 + dx < previous.width && y * 2 + dy < previous.height) {
							for (unsigned c = 0; c < 4; ++c)
								next.at(x, y)[c] +=
									previous.at(x * 2 + dx, y * 2 + dy)[c];
							++count;
						}
				for (auto &v : next.at(x, y))
					v /= count;
			}
		levels.push_back(std::move(next));
	}
	Image background = levels.back();
	const auto top = background.pixels[0];
	for (unsigned c = 0; c < 3; ++c)
		background.pixels[0][c] = top[3] > 1e-6 ? top[c] / top[3] : 0;
	background.pixels[0][3] = 1;
	for (std::size_t i = levels.size() - 1; i-- > 0;) {
		auto next = levels[i];
		for (unsigned y = 0; y < next.height; ++y)
			for (unsigned x = 0; x < next.width; ++x) {
				auto coarse = sample(background, (x + 0.5) / next.width, (y + 0.5) / next.height);
				auto &p = next.at(x, y);
				for (unsigned c = 0; c < 3; ++c)
					p[c] += (1 - unit(p[3])) * coarse[c];
				p[3] = 1;
			}
		background = std::move(next);
	}
	for (auto &pixel : background.pixels)
		for (unsigned c = 0; c < 3; ++c)
			pixel[c] = encode(pixel[c]);
	Image raw(native.width, native.height), output(native.width, native.height);
	for (unsigned y = 0; y < native.height; ++y)
		for (unsigned x = 0; x < native.width; ++x) {
			const double u = (x + 0.5) / native.width, v = (y + 0.5) / native.height;
			const double ru = (u - roi.left) / (roi.right - roi.left),
				     rv = (v - roi.top) / (roi.bottom - roi.top);
			const auto coef = sample(coefficients, ru, rv), correction = sample(delta, ru, rv),
				   bg = sample(background, ru, rv);
			auto rgb = native.at(x, y);
			for (unsigned c = 0; c < 3; ++c)
				rgb[c] = rgb[3] > 1e-6 ? unit(rgb[c] / rgb[3]) : 0;
			double a = coef[3];
			for (unsigned c = 0; c < 3; ++c)
				a += coef[c] * rgb[c];
			a = unit(a);
			if (q.guide_mode == corridorkey::GuideMode::bilinear)
				a = unit(sample(model, ru, rv)[3]);
			const auto amount = std::lerp(1.0, static_cast<double>(q.unmix_inner), unit((a - 0.9) / 0.09));
			Pixel p{0, 0, 0, a};
			for (unsigned c = 0; c < 3; ++c)
				p[c] = unit(q.unmix_mode == corridorkey::UnmixMode::delta
						    ? rgb[c] + correction[c] * amount
						    : std::lerp(rgb[c],
								(rgb[c] - (1 - a) * bg[c]) / std::max(a, 0.0001),
								amount));
			raw.at(x, y) = p;
			a = unit((a - 0.5) * q.alpha_contrast + 0.5 + q.alpha_offset);
			a = unit((a - q.clip_black) / std::max(static_cast<double>(q.clip_white - q.clip_black), 1e-6));
			a *= unit(rgb[3]) * inside(u, v, roi) * inside(u, v, settings.context.crop);
			const auto spill = std::max(p[1] - (p[0] + p[2]) / 2, 0.0) * q.despill_strength;
			p[0] += spill / 2;
			p[1] -= spill;
			p[2] += spill / 2;
			for (unsigned c = 0; c < 3; ++c)
				p[c] = decode(p[c]) * a;
			p[3] = a;
			output.at(x, y) = p;
		}
	return {std::move(coefficients), std::move(delta), std::move(background),
		std::move(band),         std::move(raw),   std::move(output)};
}
} // namespace post_reference
