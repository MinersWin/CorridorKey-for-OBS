// SPDX-License-Identifier: GPL-3.0-or-later
#include "temporal_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace corridorkey::ck2 {
Crop propose_roi(const Tensor &alpha, const FrameTransform &transform, Crop previous, float padding, float smoothing)
{
	const auto &crop = transform.user_crop;
	const auto &roi = transform.sampled_roi;
	const unsigned fw = std::max(1U, transform.source_size.width / 4),
		       fh = std::max(1U, transform.source_size.height / 4);
	const auto x0 = std::min(fw - 1, static_cast<unsigned>(std::lround(roi.left * static_cast<float>(fw))));
	const auto y0 = std::min(fh - 1, static_cast<unsigned>(std::lround(roi.top * static_cast<float>(fh))));
	const auto x1 = std::clamp(static_cast<unsigned>(std::lround(roi.right * static_cast<float>(fw))), x0 + 1, fw);
	const auto y1 = std::clamp(static_cast<unsigned>(std::lround(roi.bottom * static_cast<float>(fh))), y0 + 1, fh);
	const auto quarter = resize_bilinear(alpha, {x1 - x0, y1 - y0});
	unsigned left = fw, top = fh, right = 0, bottom = 0;
	for (unsigned y = y0; y < y1; ++y)
		for (unsigned x = x0; x < x1; ++x)
			if (quarter.values[static_cast<std::size_t>(y - y0) * (x1 - x0) + x - x0] > .1f) {
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x + 1);
				bottom = std::max(bottom, y + 1);
			}
	Crop target = crop;
	if (right > left && bottom > top) {
		const float l = static_cast<float>(left) / static_cast<float>(fw),
			    r = static_cast<float>(right) / static_cast<float>(fw);
		const float t = static_cast<float>(top) / static_cast<float>(fh),
			    b = static_cast<float>(bottom) / static_cast<float>(fh);
		const auto width = std::min(crop.right - crop.left, std::max(.35f, (r - l) * (1 + 2 * padding)));
		const auto height = std::min(crop.bottom - crop.top, std::max(.35f, (b - t) * (1 + 2 * padding)));
		target.left = std::clamp((l + r - width) * .5f, crop.left, crop.right - width);
		target.top = std::clamp((t + b - height) * .5f, crop.top, crop.bottom - height);
		target.right = target.left + width;
		target.bottom = target.top + height;
	}
	return {std::lerp(target.left, previous.left, smoothing), std::lerp(target.top, previous.top, smoothing),
		std::lerp(target.right, previous.right, smoothing),
		std::lerp(target.bottom, previous.bottom, smoothing)};
}
SequenceState::SequenceState(Extent grid, bool quality, Graphs graphs)
	: grid_(grid),
	  quality_(quality),
	  graphs_(std::move(graphs)),
	  stage1_inputs_(input_contract(Stage::stage1, grid)),
	  recurrent_(input_contract(Stage::rvm, grid).at("state"))
{
	for (auto stage : {Stage::moge, Stage::rvm, Stage::stage1})
		if (!graphs_.contains(stage))
			throw std::invalid_argument("Missing required CK2 graph");
	if (quality && !graphs_.contains(Stage::stage2))
		throw std::invalid_argument("Quality requires Stage 2");
}
void SequenceState::reset()
{
	for (auto &[stage, graph] : graphs_) {
		(void)stage;
		graph->reset_transform();
	}
	std::fill(recurrent_.values.begin(), recurrent_.values.end(), 0.f);
	moge_valid_ = false;
	key_.reset();
	processed_ = since_moge_ = 0;
	next_roi_ = {};
}
Tensors SequenceState::run(Stage stage, const Inputs &inputs, std::stop_token stop, double &milliseconds)
{
	if (stop.stop_requested() || (cancel_ && cancel_->load()))
		throw std::runtime_error("CK2 cancelled between stages");
	const auto start = Clock::now();
	auto outputs = graphs_.at(stage)->run(inputs, stop);
	milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	const auto expected = output_contract(stage, grid_);
	if (outputs.size() != expected.size())
		throw std::runtime_error("CK2 output count mismatch");
	for (const auto &[name, spec] : expected)
		check_tensor(outputs.at(name), spec, name == "alpha" || name == "fg");
	return outputs;
}
LowResolutionResult SequenceState::process(const InputFrame &input, std::stop_token stop,
					   const std::atomic<bool> *cancel)
{
	cancel_ = cancel;
	if (!input.settings || input.analysis.size != grid_ || input.settings->quality.grid != grid_ ||
	    (input.settings->quality.profile == Profile::quality) != quality_)
		throw std::invalid_argument("Frame does not match prepared CK2 profile");
	const auto &q = input.settings->quality;
	const bool discontinuity =
		key_ && (key_->instance != input.identity.instance || key_->generation != input.identity.generation ||
			 key_->grid != grid_ || key_->source_color != input.original.color ||
			 key_->transform.source_size != input.transform.source_size ||
			 key_->transform.user_crop != input.transform.user_crop ||
			 input.identity.captured_at - key_->last_captured_at > std::chrono::milliseconds(500));
	if (discontinuity)
		reset();
	if (key_ &&
	    (input.identity.sequence <= key_->last_accepted || input.identity.captured_at < key_->last_captured_at))
		throw std::invalid_argument("CK2 requires ordered unique frames");
	if (!key_)
		next_roi_ = input.transform.user_crop;
	if (key_ && key_->transform != input.transform) {
		for (auto &[stage, graph] : graphs_) {
			(void)stage;
			graph->reset_transform();
		}
		std::fill(recurrent_.values.begin(), recurrent_.values.end(), 0.f);
		moge_valid_ = false;
	}
	try {
		auto rgb = planar_rgb(input.analysis);
		auto thumb = resize_area(rgb, {32, 32});
		float motion = 0;
		if (moge_valid_)
			for (std::size_t i = 0; i < thumb.values.size(); ++i)
				motion += std::abs(thumb.values[i] - thumbnail_.values[i]);
		motion /= static_cast<float>(thumb.values.size());
		LowResolutionResult result;
		result.transform = input.transform;
		const auto interval = std::clamp(q.moge_interval, 1U, 8U);
		const bool refresh = !moge_valid_ || since_moge_ >= interval ||
				     (q.moge_motion_threshold > 0 && motion > q.moge_motion_threshold);
		if (refresh) {
			auto small = resize_area(rgb, {grid_.width * 7 / 32, grid_.height * 7 / 32});
			auto output = run(Stage::moge, {{"rgb_moge", &small}}, stop, result.timings.moge_ms);
			moge_ = std::move(output.at("moge_feat"));
			thumbnail_ = std::move(thumb);
			moge_valid_ = true;
			since_moge_ = 0;
		}
		result.timings.moge_refreshed = refresh;
		if (!q.use_rvm_temporal)
			std::fill(recurrent_.values.begin(), recurrent_.values.end(), 0.f);
		auto rvm_rgb = resize_area(rgb, {grid_.width / 4, grid_.height / 4});
		auto rvm =
			run(Stage::rvm, {{"rgb_rvm", &rvm_rgb}, {"state", &recurrent_}}, stop, result.timings.rvm_ms);
		auto s1 = run(Stage::stage1,
			      {{"rgb", &rgb},
			       {"moge_feat", &moge_},
			       {"rvm_feat", &rvm.at("rvm_feat")},
			       {"hint", &stage1_inputs_.at("hint")},
			       {"hint_quality", &stage1_inputs_.at("hint_quality")}},
			      stop, result.timings.stage1_ms);
		auto decoded = quality_ ? run(Stage::stage2, {{"rgb", &rgb}, {"baton", &s1.at("baton")}}, stop,
					      result.timings.stage2_ms)
					: std::move(s1);
		result.alpha = std::move(decoded.at("alpha"));
		result.foreground = std::move(decoded.at("fg"));
		if (q.profile != Profile::facecam && q.roi_enabled && processed_ % 4 == 0)
			next_roi_ = propose_roi(result.alpha, input.transform, next_roi_,
						std::clamp(q.roi_padding, 0.f, 1.f),
						std::clamp(q.roi_smoothing, 0.f, 1.f));
		else if (!q.roi_enabled || q.profile == Profile::facecam)
			next_roi_ = input.transform.user_crop;
		result.next_roi = next_roi_;
		if (stop.stop_requested() || input.cancellation.stop_requested() || (cancel_ && cancel_->load()))
			throw std::runtime_error("CK2 frame cancelled");
		recurrent_ = std::move(rvm.at("state_out"));
		key_ = TemporalStateKey{input.identity.instance,   input.identity.generation,
					input.identity.sequence,   grid_,
					input.original.color,      input.transform,
					input.identity.captured_at};
		++processed_;
		++since_moge_;
		return result;
	} catch (...) {
		reset();
		throw;
	}
}
} // namespace corridorkey::ck2
