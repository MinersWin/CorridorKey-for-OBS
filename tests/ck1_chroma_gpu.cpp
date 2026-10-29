// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/hints/chroma_obs.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>

int run_ck1_chroma_tests(const char *directory)
{
	using namespace corridorkey;
	try {
		auto hint = ck1::obs_chroma_hint({256, 256},
						 (std::string(directory) + "/effects/ck1_chroma.effect").c_str());
		const std::array<std::array<float, 3>, 5> colors{
			{{0, 1, 0}, {0, 0, 1}, {.5f, .5f, .5f}, {1, 0, 0}, {0, .6f, .6f}}};
		ck2::Tensor rgb({1, 3, 256, 256});
		for (std::size_t p = 0; p < 256 * 256; ++p)
			for (std::size_t c = 0; c < 3; ++c)
				rgb.values[c * 256 * 256 + p] = colors[p % colors.size()][c];
		Settings settings;
		settings.quality.model = ModelFamily::ck1;
		settings.hint.source = HintSource::chroma;
		for (const auto screen : {ScreenColor::green, ScreenColor::blue}) {
			settings.quality.screen_color = screen;
			const auto result = hint(rgb, settings);
			double maximum = 0;
			for (std::size_t p = 0; p < result.values.size(); ++p) {
				const auto background = screen == ScreenColor::green ? 0U : 1U;
				const auto expected = p % colors.size() == background ? 0.f : 1.f;
				maximum = std::max(maximum, std::abs(static_cast<double>(result.values[p] - expected)));
			}
			std::printf("CK1 CHROMA %s max=%.9g %s\n", screen == ScreenColor::green ? "green" : "blue",
				    maximum, maximum < 1e-6 ? "PASS" : "FAIL");
			if (maximum >= 1e-6)
				return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::fprintf(stderr, "CK1 chroma shader: %s\n", e.what());
		return 1;
	}
}
