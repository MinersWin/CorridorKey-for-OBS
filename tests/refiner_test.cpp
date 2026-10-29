// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/refiner_stage.hpp"
#include <doctest/doctest.h>
#include <limits>
#include <thread>

using namespace corridorkey;
TEST_CASE("refiner static contracts and atlas channel layout")
{
	for (const unsigned budget : {256U, 384U, 512U, 768U}) {
		CHECK(ck2::graph_name(ck2::Stage::refiner, {budget, 52}) == "refiner_b" + std::to_string(budget));
		CHECK(ck2::input_contract(ck2::Stage::refiner, {budget, 52}).at("x").shape ==
		      std::vector<std::int64_t>{budget, 11, 52, 52});
	}
	CHECK_THROWS(refiner::input_atlas(257));
	CHECK_THROWS(refiner::unpack_atlas({}, 256));
	const auto size = refiner::input_atlas(256);
	std::vector<float> atlas(static_cast<std::size_t>(size.width) * size.height * 4);
	for (unsigned c = 0; c < 11; ++c)
		for (unsigned y = 0; y < size.height / 3; ++y)
			for (unsigned x = 0; x < size.width; ++x)
				atlas[((c / 4 * (size.height / 3) + y) * size.width + x) * 4 + c % 4] = float(c) / 16;
	const auto tensor = refiner::unpack_atlas(atlas, 256);
	for (unsigned b : {0U, 15U, 16U, 255U})
		for (unsigned c = 0; c < 11; ++c)
			for (unsigned p : {0U, 51U, 52U, 2703U})
				CHECK(tensor.values[(b * 11 + c) * 2704 + p] == float(c) / 16);
	const std::vector<std::pair<float, float>> rounding{{0.0f, 0.0f},
							    {-0.0f, -0.0f},
							    {0x1p-25f, 0.0f},
							    {0x1.8p-24f, 0x1p-23f},
							    {0x1p-24f, 0x1p-24f},
							    {0x1.ffcp-15f, 0x1p-14f},
							    {0.500244140625f, 0.5f},
							    {0.500732421875f, 0.5009765625f},
							    {0.999755859375f, 1.0f}};
	for (std::size_t i = 0; i < rounding.size(); ++i)
		atlas[i * 4] = rounding[i].first;
	const auto rounded = refiner::unpack_atlas(atlas, 256);
	for (std::size_t i = 0; i < rounding.size(); ++i)
		CHECK(rounded.values[i] == rounding[i].second);
	atlas[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK_THROWS(refiner::unpack_atlas(atlas, 256));
}
TEST_CASE("refiner writes only centre pixels and rejects invalid outputs")
{
	auto tensors = ck2::output_contract(ck2::Stage::refiner, {256, 52});
	for (auto &[name, value] : tensors) {
		const unsigned channels = name == "fg" ? 3 : 1;
		for (unsigned b = 0; b < 256; ++b)
			for (unsigned c = 0; c < channels; ++c)
				for (unsigned y = 10; y < 42; ++y)
					for (unsigned x = 10; x < 42; ++x)
						value.values[((b * channels + c) * 52 + y) * 52 + x] =
							name == "fg" ? float(c + 1) / 4 : float(b) / 256;
	}
	const auto centres = refiner::pack_centres(tensors, 256);
	for (unsigned b : {0U, 15U, 16U, 255U}) {
		const auto offset = ((b / 16 * 32 + 31) * 512 + b % 16 * 32 + 31) * 4;
		CHECK(centres[offset] == .25f);
		CHECK(centres[offset + 1] == .5f);
		CHECK(centres[offset + 2] == .75f);
		CHECK(centres[offset + 3] == float(b) / 256);
	}
	tensors.at("alpha").values.back() = 1.01f;
	CHECK_THROWS(refiner::pack_centres(tensors, 256));
	tensors.at("alpha").values.back() = std::numeric_limits<float>::quiet_NaN();
	CHECK_THROWS(refiner::pack_centres(tensors, 256));
}
TEST_CASE("tensor validation rejects nonfinite state without rejecting finite extremes")
{
	ck2::Tensor tensor({1});
	for (float value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
			    std::numeric_limits<float>::quiet_NaN()}) {
		tensor.values[0] = value;
		CHECK_THROWS(ck2::check_tensor(tensor, tensor));
	}
	for (float value : {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -0.0f}) {
		tensor.values[0] = value;
		CHECK_NOTHROW(ck2::check_tensor(tensor, tensor));
	}
}
TEST_CASE("refiner worker keeps admission bounded and reports inference errors")
{
	class Throwing final : public ck2::Graph {
		ck2::Tensors run(const ck2::Inputs &, std::stop_token) override
		{
			throw std::runtime_error("fixture failure");
		}
	};
	refiner::Stage stage(std::make_unique<Throwing>(), 256);
	const auto size = refiner::input_atlas(256);
	std::vector<float> atlas(static_cast<std::size_t>(size.width) * size.height * 4);
	REQUIRE(stage.submit(atlas));
	CHECK_FALSE(stage.submit(atlas));
	std::optional<refiner::Result> result;
	const auto deadline = Clock::now() + std::chrono::seconds(5);
	while (!result && Clock::now() < deadline) {
		result = stage.poll();
		std::this_thread::yield();
	}
	REQUIRE(result.has_value());
	CHECK(result->failure == "fixture failure");
	CHECK(result->rgba.empty());
}
TEST_CASE("refiner retains its model lease until the session is destroyed")
{
	struct Lease final : ModelReadLease {};
	struct Idle final : ck2::Graph {
		ck2::Tensors run(const ck2::Inputs &, std::stop_token) override { return {}; }
	};
	auto lease = std::make_shared<Lease>();
	std::weak_ptr<const ModelReadLease> observed = lease;
	{
		refiner::Stage stage(std::make_unique<Idle>(), 256, lease);
		lease.reset();
		CHECK_FALSE(observed.expired());
	}
	CHECK(observed.expired());
}
