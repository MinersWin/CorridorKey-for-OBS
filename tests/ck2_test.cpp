// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ck2_pipeline.hpp"
#include "engine/fake_pipeline.hpp"
#include "telemetry/frame_timing.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <limits>

using namespace corridorkey;
namespace {
struct Observation {
	std::map<ck2::Stage, unsigned> calls;
	std::vector<float> states;
	unsigned recaptures = 0;
	bool corrupt = false;
};
class Graph final : public ck2::Graph {
	ck2::Stage stage_;
	Extent grid_;
	std::shared_ptr<Observation> observation_;

public:
	void reset_transform() override { ++observation_->recaptures; }
	Graph(ck2::Stage stage, Extent grid, std::shared_ptr<Observation> observation)
		: stage_(stage),
		  grid_(grid),
		  observation_(std::move(observation))
	{
	}
	ck2::Tensors run(const ck2::Inputs &inputs, std::stop_token) override
	{
		++observation_->calls[stage_];
		auto outputs = ck2::output_contract(stage_, grid_);
		if (stage_ == ck2::Stage::rvm) {
			const auto state = inputs.at("state")->values[0];
			observation_->states.push_back(state);
			std::fill(outputs.at("state_out").values.begin(), outputs.at("state_out").values.end(),
				  state + 1.f);
		}
		if (stage_ == ck2::Stage::stage1 || stage_ == ck2::Stage::stage2) {
			std::fill(outputs.at("alpha").values.begin(), outputs.at("alpha").values.end(), .6f);
			std::fill(outputs.at("fg").values.begin(), outputs.at("fg").values.end(), .4f);
			if (observation_->corrupt)
				outputs.at("alpha").values[0] = std::numeric_limits<float>::quiet_NaN();
		}
		return outputs;
	}
};
ck2::Graphs graphs(const std::shared_ptr<Observation> &observation, Extent grid = {512, 256})
{
	ck2::Graphs result;
	for (const auto stage : {ck2::Stage::moge, ck2::Stage::rvm, ck2::Stage::stage1, ck2::Stage::stage2})
		result.emplace(stage, std::make_unique<Graph>(stage, grid, observation));
	return result;
}
InputFrame frame(FramePool &pool, Sequence sequence, float color = .2f, Profile profile = Profile::facecam)
{
	InputFrame input;
	input.lease = std::move(*pool.try_acquire());
	input.lease.transition(FrameState::ready);
	input.identity = {1, pool.generation(), sequence, sequence,
			  TimePoint{} + std::chrono::milliseconds(sequence * 16)};
	input.transform.source_size = {1920, 1080};
	auto settings = std::make_shared<Settings>();
	settings->quality.profile = profile;
	settings->quality.grid = {512, 256};
	input.settings = settings;
	input.analysis = ck2::packed_image(ck2::Tensor({1, 3, 256, 512}, color), PixelFormat::rgb_f32);
	input.original = input.analysis;
	input.original.size = input.transform.source_size;
	return input;
}
} // namespace
TEST_CASE("CK2 recurrent sequence and cache follow accepted frames")
{
	FramePool pool;
	auto observed = std::make_shared<Observation>();
	ck2::SequenceState sequence({512, 256}, false, graphs(observed));
	for (unsigned i = 1; i <= 7; ++i) {
		auto input = frame(pool, i * 10);
		sequence.process(input);
	}
	CHECK(observed->calls[ck2::Stage::moge] == 3);
	CHECK(observed->calls[ck2::Stage::rvm] == 7);
	CHECK(observed->states == std::vector<float>{0, 1, 2, 3, 4, 5, 6});
	CHECK(observed->calls[ck2::Stage::stage2] == 0);
}
TEST_CASE("CK2 motion compares with last MoGe refresh and does not reset RVM")
{
	FramePool pool;
	auto observed = std::make_shared<Observation>();
	ck2::SequenceState sequence({512, 256}, false, graphs(observed));
	for (unsigned i = 1; i <= 3; ++i) {
		auto input = frame(pool, i, .2f + static_cast<float>(i - 1) * .03f);
		sequence.process(input);
	}
	CHECK(observed->calls[ck2::Stage::moge] == 2);
	CHECK(observed->states == std::vector<float>{0, 1, 2});
}
TEST_CASE("CK2 transform gap generation and explicit cut reset foundations")
{
	FramePool pool;
	auto observed = std::make_shared<Observation>();
	ck2::SequenceState sequence({512, 256}, false, graphs(observed));
	{
		auto input = frame(pool, 1);
		sequence.process(input);
		CHECK_THROWS(sequence.process(input));
	}
	{
		auto input = frame(pool, 2);
		input.transform.sampled_roi = {.1f, .1f, .9f, .9f};
		sequence.process(input);
	}
	{
		auto input = frame(pool, 100);
		sequence.process(input);
	}
	sequence.reset();
	{
		auto input = frame(pool, 101);
		sequence.process(input);
	}
	pool.advance_generation();
	{
		auto input = frame(pool, 102);
		sequence.process(input);
	}
	CHECK(observed->states == std::vector<float>{0, 0, 0, 0, 0});
	CHECK(observed->calls[ck2::Stage::moge] == 5);
	CHECK(observed->recaptures == 8);
}
TEST_CASE("CK2 slow frames reset temporal state without repeated capture")
{
	FramePool pool;
	auto observed = std::make_shared<Observation>();
	ck2::SequenceState sequence({512, 256}, false, graphs(observed));
	for (unsigned i : {1U, 100U, 200U, 201U}) {
		auto input = frame(pool, i);
		sequence.process(input);
	}
	CHECK(observed->states == std::vector<float>{0, 0, 0, 1});
	CHECK(observed->recaptures == 0);
}
TEST_CASE("CK2 Quality invokes Stage 2 and instances never share recurrence")
{
	FramePool a, b;
	auto oa = std::make_shared<Observation>(), ob = std::make_shared<Observation>();
	ck2::SequenceState sa({512, 256}, true, graphs(oa)), sb({512, 256}, false, graphs(ob));
	{
		auto input = frame(a, 1, .2f, Profile::quality);
		const auto out = sa.process(input);
		CHECK(out.alpha.shape == std::vector<std::int64_t>{1, 1, 256, 512});
	}
	{
		auto input = frame(b, 1);
		sb.process(input);
	}
	CHECK(oa->calls[ck2::Stage::stage2] == 1);
	CHECK(ob->states == std::vector<float>{0});
	oa->corrupt = true;
	{
		auto input = frame(a, 2, .2f, Profile::quality);
		CHECK_THROWS(sa.process(input));
	}
	oa->corrupt = false;
	{
		auto input = frame(a, 3, .2f, Profile::quality);
		sa.process(input);
	}
	CHECK(oa->states.back() == 0);
}
TEST_CASE("CK2 ROI pads minimum sides and empty alpha expands to user crop")
{
	ck2::Tensor alpha({1, 1, 100, 100});
	alpha.values[5050] = 1;
	FrameTransform transform;
	transform.source_size = {400, 400};
	const auto roi = ck2::propose_roi(alpha, transform, {}, .2f, 0);
	CHECK(roi.right - roi.left == doctest::Approx(.35f));
	CHECK(roi.bottom - roi.top == doctest::Approx(.35f));
	const auto smoothed = ck2::propose_roi(alpha, transform, {}, .2f, .8f);
	CHECK(smoothed.left == doctest::Approx(roi.left * .2f));
	std::fill(alpha.values.begin(), alpha.values.end(), 0.f);
	const auto empty = ck2::propose_roi(alpha, transform, roi, .2f, .8f);
	CHECK(empty.left < roi.left);
	CHECK(empty.right > roi.right);
}
TEST_CASE("CK2 area preprocessing and half pixel reconstruction")
{
	ck2::Tensor input({1, 1, 2, 4});
	input.values = {0, 1, 2, 3, 4, 5, 6, 7};
	CHECK(ck2::resize_area(input, {2, 1}).values == std::vector<float>{2.5f, 4.5f});
	ck2::Tensor line({1, 1, 1, 2});
	line.values = {0, 1};
	CHECK(ck2::resize_bilinear(line, {4, 1}).values == std::vector<float>{0, .25f, .75f, 1});
}
TEST_CASE("CK2 pipeline retains input lease and rejects duplicate submissions")
{
	auto observed = std::make_shared<Observation>();
	Ck2Pipeline pipeline(
		[&](const PrepareRequest &) { return Ck2Prepared{graphs(observed), Backend::fake, "test"}; });
	FramePool pool;
	auto input = frame(pool, 1);
	PrepareRequest request;
	request.instance = 1;
	request.generation = pool.generation();
	request.settings = input.settings;
	REQUIRE_FALSE(pipeline.prepare(request));
	REQUIRE_FALSE(pipeline.try_submit(input, Clock::now()));
	CHECK_FALSE(input.lease);
	std::optional<FrameResult> result;
	const auto deadline = Clock::now() + std::chrono::seconds(3);
	while (!(result = pipeline.poll_result()) && Clock::now() < deadline)
		std::this_thread::yield();
	REQUIRE(result);
	CHECK_FALSE(result->failure);
	CHECK_FALSE(validate_result(*result));
	CHECK(pool.in_use() == 1);
	{
		auto duplicate = frame(pool, 1);
		CHECK(pipeline.try_submit(duplicate, Clock::now()).code == ErrorCode::invalid_input);
	}
	pipeline.cancel();
	CHECK(pool.in_use() == 1);
	result.reset();
	CHECK(pool.in_use() == 0);
}
TEST_CASE("CK2 cancellation reset accepts a new generation and checks changed settings")
{
	auto observed = std::make_shared<Observation>();
	Ck2Pipeline pipeline(
		[&](const PrepareRequest &) { return Ck2Prepared{graphs(observed), Backend::fake, "test"}; });
	FramePool pool;
	auto initial = frame(pool, 1);
	std::stop_source stop;
	PrepareRequest request;
	request.instance = 1;
	request.generation = pool.generation();
	request.settings = initial.settings;
	request.cancellation = stop.get_token();
	REQUIRE_FALSE(pipeline.prepare(request));
	stop.request_stop();
	CHECK(pipeline.try_submit(initial, Clock::now()).code == ErrorCode::cancelled);
	initial = {};
	pool.advance_generation();
	REQUIRE_FALSE(pipeline.reset(pool.generation(), ResetReason::explicit_reset));
	auto input = frame(pool, 1);
	auto settings = std::make_shared<Settings>(*input.settings);
	settings->quality.aspect_fix = true;
	input.settings = settings;
	CHECK(pipeline.try_submit(input, Clock::now()).code == ErrorCode::unsupported);
	CHECK(static_cast<bool>(input.lease));
	settings->quality.aspect_fix = false;
	REQUIRE_FALSE(pipeline.try_submit(input, Clock::now()));
	std::optional<FrameResult> result;
	const auto deadline = Clock::now() + std::chrono::seconds(3);
	while (!(result = pipeline.poll_result()) && Clock::now() < deadline)
		std::this_thread::yield();
	REQUIRE(result);
	CHECK_FALSE(result->failure);
}
TEST_CASE("CK2 low-resolution outputs and timing use the host clock epoch")
{
	auto observed = std::make_shared<Observation>();
	std::atomic<int> milliseconds{110};
	Ck2Pipeline pipeline(
		[&](const PrepareRequest &) { return Ck2Prepared{graphs(observed), Backend::fake, "test"}; },
		[&] { return TimePoint{} + std::chrono::milliseconds(milliseconds.fetch_add(5)); });
	FramePool pool;
	auto input = frame(pool, 1);
	input.identity.captured_at = TimePoint{} + std::chrono::milliseconds(100);
	PrepareRequest request;
	request.instance = 1;
	request.generation = pool.generation();
	request.settings = input.settings;
	REQUIRE_FALSE(pipeline.prepare(request));
	REQUIRE_FALSE(pipeline.try_submit(input, TimePoint{} + std::chrono::milliseconds(105)));
	std::optional<FrameResult> result;
	const auto deadline = Clock::now() + std::chrono::seconds(3);
	while (!(result = pipeline.poll_result()) && Clock::now() < deadline)
		std::this_thread::yield();
	REQUIRE(result);
	REQUIRE_FALSE(result->failure);
	CHECK(result->alpha.size == Extent{256, 128});
	CHECK(result->foreground.size == result->alpha.size);
	CHECK_FALSE(validate_result(*result));
	CHECK(result->timings.started_at == TimePoint{} + std::chrono::milliseconds(110));
	CHECK(result->timings.completed_at == TimePoint{} + std::chrono::milliseconds(115));
	FrameTiming timing;
	const auto presented = TimePoint{} + std::chrono::milliseconds(125);
	timing.record(*result, presented, 1);
	const auto snapshot = timing.snapshot(presented);
	CHECK(snapshot.age_median_ms == std::optional<double>{25});
	CHECK(snapshot.inference_median_ms == std::optional<double>{5});
	CHECK(snapshot.queue_median_ms == std::optional<double>{5});
	result->alpha.size.width /= 2;
	result->foreground.size = result->alpha.size;
	CHECK(validate_result(*result).code == ErrorCode::invalid_output);
}
