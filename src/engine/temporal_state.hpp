// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ck2_graph.hpp"
#include <optional>
#include <atomic>

namespace corridorkey::ck2 {
Crop propose_roi(const Tensor &alpha, const FrameTransform &, Crop previous, float padding, float smoothing);
class SequenceState {
public:
	SequenceState(Extent grid, bool quality, Graphs graphs);
	LowResolutionResult process(const InputFrame &, std::stop_token = {},
				    const std::atomic<bool> *cancel = nullptr);
	void reset();
	std::uint64_t processed() const { return processed_; }

private:
	Tensors run(Stage, const Inputs &, std::stop_token, double &milliseconds);
	Extent grid_;
	bool quality_;
	Graphs graphs_;
	Tensors stage1_inputs_;
	Tensor recurrent_, moge_, thumbnail_;
	Tensor motion_rgb_, moge_rgb_, rvm_rgb_;
	std::optional<TemporalStateKey> key_;
	std::optional<FrameTransform> captured_transform_;
	std::uint64_t processed_ = 0, since_moge_ = 0;
	bool moge_valid_ = false;
	const std::atomic<bool> *cancel_ = nullptr;
	Crop next_roi_;
};
} // namespace corridorkey::ck2
