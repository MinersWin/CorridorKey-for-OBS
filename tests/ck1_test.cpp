// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ck1_pipeline.hpp"
#include "engine/ck2_pipeline.hpp"
#include "runtime/scheduler.hpp"
#include "settings/schema.hpp"
#include <doctest/doctest.h>
#include <limits>

using namespace corridorkey;
namespace {
struct Seen {
	unsigned hint = 0, key = 0;
	float rgb = 0, hint_value = 0;
	bool corrupt = false;
};
class Graph final : public ck2::Graph {
public:
	Graph(std::shared_ptr<Seen> seen, bool hint, Extent grid) : seen_(seen), hint_(hint), grid_(grid) {}
	ck2::Tensors run(const ck2::Inputs &in, std::stop_token) override
	{
		auto out = ck1::output_contract(grid_, hint_);
		seen_->rgb = in.at("rgb")->values.front();
		if (hint_) {
			++seen_->hint;
			std::fill(out.at("matte").values.begin(), out.at("matte").values.end(), .3f);
		} else {
			++seen_->key;
			seen_->hint_value = in.at("hint")->values.front();
			out.at("alpha") = *in.at("hint");
			out.at("fg") = *in.at("rgb");
		}
		if (seen_->corrupt)
			out.begin()->second.values.front() = std::numeric_limits<float>::quiet_NaN();
		return out;
	}

private:
	std::shared_ptr<Seen> seen_;
	bool hint_;
	Extent grid_;
};
ck1::Graphs graphs(const Settings &settings, const std::shared_ptr<Seen> &seen)
{
	ck1::Graphs g;
	g.key = std::make_unique<Graph>(seen, false, settings.quality.grid);
	g.modnet = std::make_unique<Graph>(seen, true, settings.quality.grid);
	return g;
}
InputFrame frame(FramePool &pool, const Settings &settings, Sequence sequence = 1)
{
	InputFrame f;
	f.lease = std::move(*pool.try_acquire());
	f.lease.transition(FrameState::ready);
	f.identity = {1, pool.generation(), sequence, sequence, Clock::now()};
	f.settings = std::make_shared<Settings>(settings);
	f.transform = {{1920, 1080}, {.1f, .2f, .9f, .95f}, {.1f, .2f, .9f, .95f}, false};
	f.analysis =
		ck2::packed_image(ck2::Tensor({1, 3, settings.quality.grid.height, settings.quality.grid.width}, .7f),
				  PixelFormat::rgb_f32);
	f.original = f.analysis;
	f.original.size = f.transform.source_size;
	return f;
}
} // namespace
TEST_CASE("CK1 and CK2 workers share admission without sharing frames or recurrent state")
{
	struct RvmState {
		std::vector<float> values;
	};
	class Ck2Graph final : public ck2::Graph {
	public:
		Ck2Graph(ck2::Stage stage, Extent grid, RvmState &state) : stage_(stage), grid_(grid), state_(state) {}
		ck2::Tensors run(const ck2::Inputs &inputs, std::stop_token) override
		{
			auto output = ck2::output_contract(stage_, grid_);
			if (stage_ == ck2::Stage::rvm) {
				const auto value = inputs.at("state")->values.front();
				state_.values.push_back(value);
				std::fill(output.at("state_out").values.begin(), output.at("state_out").values.end(),
					  value + 1);
			}
			if (stage_ == ck2::Stage::stage1)
				std::fill(output.at("alpha").values.begin(), output.at("alpha").values.end(), .8f);
			return output;
		}

	private:
		ck2::Stage stage_;
		Extent grid_;
		RvmState &state_;
	};
	auto first = settings::defaults(Profile::custom, ModelFamily::ck1);
	first.quality.grid = {256, 256};
	auto second = settings::defaults(Profile::facecam, ModelFamily::ck2);
	second.quality.refiner_enabled = false;
	second.quality.roi_enabled = false;
	auto seen = std::make_shared<Seen>();
	RvmState state;
	Ck1Pipeline ck1([&](const PrepareRequest &) {
		Ck1Prepared prepared;
		prepared.graphs = graphs(first, seen);
		return prepared;
	});
	Ck2Pipeline ck2([&](const PrepareRequest &) {
		Ck2Prepared prepared;
		for (const auto stage : {ck2::Stage::moge, ck2::Stage::rvm, ck2::Stage::stage1})
			prepared.graphs.emplace(stage, std::make_unique<Ck2Graph>(stage, second.quality.grid, state));
		return prepared;
	});
	FramePool pools[2];
	FairAdmission gate;
	Scheduler schedulers[] = {Scheduler(gate), Scheduler(gate)};
	ModelPipeline *pipelines[] = {&ck1, &ck2};
	Settings settings[] = {first, second};
	for (unsigned i = 0; i < 2; ++i) {
		PrepareRequest request;
		request.instance = i + 1;
		request.generation = pools[i].generation();
		request.settings = std::make_shared<Settings>(settings[i]);
		REQUIRE_FALSE(pipelines[i]->prepare(request));
	}
	for (Sequence sequence = 1; sequence <= 8; ++sequence) {
		for (unsigned i = 0; i < 2; ++i) {
			auto input = frame(pools[i], settings[i], sequence);
			input.identity.instance = i + 1;
			input.transform.source_size = i == 0 ? Extent{719, 1281} : Extent{3840, 2160};
			input.original.size = input.transform.source_size;
			schedulers[i].offer(std::move(input));
		}
		REQUIRE_FALSE(schedulers[0].submit(ck1, sequence, Clock::now()));
		CHECK(schedulers[1].submit(ck2, sequence, Clock::now()).code == ErrorCode::busy);
		for (unsigned i = 0; i < 2; ++i) {
			if (i == 1)
				REQUIRE_FALSE(schedulers[i].submit(*pipelines[i], sequence, Clock::now()));
			std::optional<FrameResult> result;
			const auto deadline = Clock::now() + std::chrono::seconds(3);
			while (!(result = pipelines[i]->poll_result()) && Clock::now() < deadline)
				std::this_thread::yield();
			REQUIRE(result);
			CHECK_FALSE(result->failure);
			CHECK(result->identity.instance == i + 1);
			CHECK(result->identity.sequence == sequence);
			CHECK(result->input.settings->quality.model == settings[i].quality.model);
			CHECK(result->input.original.size == (i == 0 ? Extent{719, 1281} : Extent{3840, 2160}));
			CHECK(result->alpha.storage->host_floats().front() == doctest::Approx(i == 0 ? .3f : .8f));
			schedulers[i].completed();
			result.reset();
			CHECK(pools[i].in_use() == 0);
		}
	}
	REQUIRE(state.values.size() == 8);
	for (unsigned i = 0; i < state.values.size(); ++i)
		CHECK(state.values[i] == doctest::Approx(i));
	CHECK(seen->hint == 8);
	CHECK(seen->key == 8);
}
TEST_CASE("CK1 uses MODNet on the same square capture and preserves native crop identity")
{
	for (unsigned side : {256U, 384U, 512U}) {
		auto settings = settings::defaults(Profile::custom, ModelFamily::ck1);
		settings.quality.grid = {side, side};
		CHECK(settings.hint.source == HintSource::modnet);
		CHECK(Ck1Pipeline::supports(settings));
		auto seen = std::make_shared<Seen>();
		ck1::SequenceState pipeline(settings, graphs(settings, seen));
		FramePool pool;
		auto input = frame(pool, settings);
		auto result = pipeline.process(input);
		CHECK(seen->hint == 1);
		CHECK(seen->key == 1);
		CHECK(seen->rgb == doctest::Approx(.7));
		CHECK(seen->hint_value == doctest::Approx(.3));
		CHECK(result.transform == input.transform);
		CHECK(result.next_roi == input.transform.user_crop);
		CHECK(result.alpha.shape == std::vector<std::int64_t>{1, 1, side, side});
		CHECK(result.foreground.values.front() == doctest::Approx(.7));
		seen->corrupt = true;
		CHECK_THROWS(pipeline.process(input));
	}
}
TEST_CASE("CK1 refuses unevaluated refinement and missing or mismatched hint providers")
{
	auto settings = settings::defaults(Profile::custom, ModelFamily::ck1);
	settings.quality.screen_color = ScreenColor::blue;
	CHECK(ck1::graph_name(settings.quality.screen_color, settings.quality.grid) == "ck1blue_384");
	CHECK_THROWS(ck1::graph_name(ScreenColor::green, {512, 256}));
	settings.quality.refiner_enabled = true;
	CHECK_FALSE(Ck1Pipeline::supports(settings));
	settings::clamp(settings);
	CHECK_FALSE(settings.quality.refiner_enabled);
	for (auto hint : {HintSource::distilled, HintSource::external_mask, HintSource::mediapipe}) {
		settings.hint.source = hint;
		CHECK_FALSE(Ck1Pipeline::supports(settings));
	}
	settings.hint.source = HintSource::ck2_stage1;
	CHECK_THROWS(ck1::SequenceState(settings, graphs(settings, std::make_shared<Seen>())));
	settings.hint.source = HintSource::modnet;
	ck1::SequenceState pipeline(settings, graphs(settings, std::make_shared<Seen>()));
	FramePool pool;
	auto input = frame(pool, settings);
	input.transform.sampled_roi = {};
	CHECK_THROWS(pipeline.process(input));
	input.transform.sampled_roi = input.transform.user_crop;
	std::stop_source stop;
	stop.request_stop();
	CHECK_THROWS(pipeline.process(input, stop.get_token()));
}
TEST_CASE("CK1 worker retains frame ownership and rejects duplicate frames after publication")
{
	auto settings = settings::defaults(Profile::custom, ModelFamily::ck1);
	auto seen = std::make_shared<Seen>();
	Ck1Pipeline pipeline([&](const PrepareRequest &) {
		Ck1Prepared p;
		p.graphs = graphs(settings, seen);
		return p;
	});
	FramePool pool;
	PrepareRequest request;
	request.instance = 1;
	request.generation = pool.generation();
	request.settings = std::make_shared<Settings>(settings);
	REQUIRE_FALSE(pipeline.prepare(request));
	auto input = frame(pool, settings);
	REQUIRE_FALSE(pipeline.try_submit(input, Clock::now()));
	std::optional<FrameResult> result;
	const auto until = Clock::now() + std::chrono::seconds(3);
	while (!(result = pipeline.poll_result()) && Clock::now() < until)
		std::this_thread::yield();
	REQUIRE(result);
	CHECK_FALSE(result->failure);
	CHECK(result->input.lease.state() == FrameState::complete);
	CHECK(result->input.original.size == Extent{1920, 1080});
	CHECK_FALSE(pipeline.refiner_stage());
	auto duplicate = frame(pool, settings);
	CHECK(pipeline.try_submit(duplicate, Clock::now()).code == ErrorCode::invalid_input);
	pipeline.cancel();
	CHECK(pipeline.try_submit(duplicate, Clock::now()).code == ErrorCode::cancelled);
}
TEST_CASE("CK1 Stage-1 hint reuses the same capture and private CK2 temporal state")
{
	struct Observation {
		std::vector<float> states;
		unsigned moge = 0, stage1 = 0;
	};
	class HintGraph final : public ck2::Graph {
	public:
		HintGraph(ck2::Stage stage, std::shared_ptr<Observation> observation)
			: stage_(stage),
			  observation_(std::move(observation))
		{
		}
		ck2::Tensors run(const ck2::Inputs &in, std::stop_token) override
		{
			auto out = ck2::output_contract(stage_, {512, 512});
			if (stage_ == ck2::Stage::moge)
				++observation_->moge;
			if (stage_ == ck2::Stage::rvm) {
				const auto state = in.at("state")->values.front();
				observation_->states.push_back(state);
				std::fill(out.at("state_out").values.begin(), out.at("state_out").values.end(),
					  state + 1);
			}
			if (stage_ == ck2::Stage::stage1) {
				++observation_->stage1;
				CHECK(in.at("rgb")->shape == std::vector<std::int64_t>{1, 3, 512, 512});
				CHECK(in.at("rgb")->values.front() == doctest::Approx(.7));
				CHECK(in.at("hint_quality")->values.front() == -1);
				std::fill(out.at("alpha").values.begin(), out.at("alpha").values.end(), .4f);
			}
			return out;
		}

	private:
		ck2::Stage stage_;
		std::shared_ptr<Observation> observation_;
	};
	auto settings = settings::defaults(Profile::custom, ModelFamily::ck1);
	settings.hint.source = HintSource::ck2_stage1;
	auto seen = std::make_shared<Seen>();
	auto observed = std::make_shared<Observation>();
	auto g = graphs(settings, seen);
	ck2::Graphs hint_graphs;
	for (auto stage : {ck2::Stage::moge, ck2::Stage::rvm, ck2::Stage::stage1})
		hint_graphs.emplace(stage, std::make_unique<HintGraph>(stage, observed));
	g.stage1 = std::make_unique<ck2::SequenceState>(Extent{512, 512}, false, std::move(hint_graphs));
	ck1::SequenceState sequence(settings, std::move(g));
	FramePool pool;
	for (unsigned i = 1; i <= 2; ++i) {
		auto input = frame(pool, settings, i);
		sequence.process(input);
	}
	CHECK(seen->hint == 0);
	CHECK(seen->hint_value == doctest::Approx(.4));
	CHECK(observed->states == std::vector<float>{0, 1});
	CHECK(observed->moge == 1);
	CHECK(observed->stage1 == 2);
	sequence.reset();
	auto input = frame(pool, settings, 3);
	sequence.process(input);
	CHECK(observed->states.back() == 0);
}
TEST_CASE("CK1 Expert chroma provider receives the selected screen and cannot reuse a stale hint")
{
	auto settings = settings::defaults(Profile::custom, ModelFamily::ck1);
	settings.hint.source = HintSource::chroma;
	settings.quality.screen_color = ScreenColor::blue;
	auto seen = std::make_shared<Seen>();
	auto g = graphs(settings, seen);
	unsigned hints = 0;
	g.chroma = [&](const ck2::Tensor &rgb, const Settings &s) {
		++hints;
		CHECK(s.quality.screen_color == ScreenColor::blue);
		CHECK(rgb.values.front() == doctest::Approx(.7));
		return ck2::Tensor({1, 1, 384, 384}, static_cast<float>(hints) * .1f);
	};
	ck1::SequenceState sequence(settings, std::move(g));
	FramePool pool;
	for (unsigned i = 1; i <= 2; ++i) {
		auto input = frame(pool, settings, i);
		sequence.process(input);
		CHECK(seen->hint_value == doctest::Approx(static_cast<double>(i) * .1));
	}
	CHECK(hints == 2);
	CHECK(seen->hint == 0);
}
