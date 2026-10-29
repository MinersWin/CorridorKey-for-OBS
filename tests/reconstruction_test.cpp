// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include <doctest/doctest.h>
#include "post_reference.hpp"
#include "runtime/frame_pool.hpp"
#include "runtime/capture_pacer.hpp"
#include <stdexcept>
#include <array>
#include <cmath>
using namespace corridorkey;
TEST_CASE("Capture caps keep their phase on a 60 Hz canvas")
{
	for (const auto fps : {0U, 15U, 24U, 30U, 60U}) {
		CapturePacer pacer;
		unsigned captures = 0;
		for (std::uint64_t frame = 0; frame < 600; ++frame) {
			const auto tick = 1000000000ULL + frame * 1000000000ULL / 60;
			if (pacer.due(tick)) {
				pacer.captured(tick, fps);
				++captures;
			}
		}
		CHECK(captures == (fps ? fps * 10 : 600));
		pacer.reset();
		CHECK(pacer.due(1));
	}
}
TEST_CASE("Five retained frames remain bounded across generation changes")
{
	FramePool pool{FramePool::maximum_capacity};
	std::array<std::optional<FrameLease>, FramePool::maximum_capacity> held;
	for (auto &lease : held) {
		lease = pool.try_acquire();
		REQUIRE(lease);
	}
	CHECK_FALSE(pool.try_acquire());
	pool.advance_generation();
	CHECK_FALSE(pool.try_acquire());
	for (auto &lease : held)
		CHECK_FALSE(lease->current());
	held[2].reset();
	auto replacement = pool.try_acquire();
	REQUIRE(replacement);
	CHECK(replacement->current());
	CHECK(replacement->id().index == 2);
	CHECK_THROWS_AS(FramePool{0}, std::invalid_argument);
	CHECK_THROWS_AS(FramePool{6}, std::invalid_argument);
}
TEST_CASE("Reference preserves source alpha and transfers no gamma twice")
{
	post_reference::Image native(3, 1), guide(1, 1), model(1, 1);
	guide.pixels[0] = model.pixels[0] = {0.5, 0.5, 0.5, 1};
	native.pixels = {{{0.5, 0.5, 0.5, 1}}, {{0.25, 0.25, 0.25, 0.5}}, {{0, 0, 0, 0}}};
	Settings settings;
	settings.quality.alpha_contrast = 1;
	const auto result = post_reference::reconstruct(native, guide, model, settings, {});
	CHECK(result.output.pixels[0][0] == doctest::Approx(0.2140411405));
	CHECK(result.output.pixels[1][0] == doctest::Approx(0.1070205702));
	CHECK(result.output.pixels[1][3] == 0.5);
	for (double value : result.output.pixels[2])
		CHECK(value == 0);
	for (double x : {0.0, 0.003, 0.04, 0.5, 1.0})
		CHECK(post_reference::encode(post_reference::decode(x)) == doctest::Approx(x));
}
TEST_CASE("Reference tiny alpha and singular guide remain finite")
{
	post_reference::Image image(1, 1), model(1, 1);
	image.pixels[0] = {0.4, 0.8, 0.3, 1};
	model.pixels[0] = {0.4, 0.8, 0.3, 1e-7};
	Settings settings;
	settings.quality.alpha_contrast = 1;
	const auto result = post_reference::reconstruct(image, image, model, settings, {});
	CHECK(result.output.pixels[0][3] == doctest::Approx(1e-7).epsilon(1e-5));
	for (double value : result.output.pixels[0])
		CHECK(std::isfinite(value));
}
TEST_CASE("Compositing equation recovers a linear-light mixture")
{
	post_reference::Image native(1, 1), guide(1, 1), model(1, 1);
	const double mixed = post_reference::encode(0.2);
	native.pixels[0] = {mixed, mixed, mixed, 1};
	guide.pixels[0] = {0, 0, 0, 1};
	model.pixels[0] = {0, 0, 0, 0.5};
	Settings settings;
	settings.quality.alpha_contrast = 1;
	settings.quality.despill_strength = 0;
	settings.quality.unmix_mode = UnmixMode::compositing_equation;
	const auto result = post_reference::reconstruct(native, guide, model, settings, {});
	CHECK(result.raw.pixels[0][0] == doctest::Approx(post_reference::encode(0.4)));
	CHECK(result.output.pixels[0][0] == doctest::Approx(0.2));
	CHECK(result.output.pixels[0][3] == doctest::Approx(0.5));
}
