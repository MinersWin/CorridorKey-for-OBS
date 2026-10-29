// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "temporal_state.hpp"
#include <functional>

namespace corridorkey::ck1 {
using ck2::Tensor;
using ck2::Tensors;
using ck2::Inputs;
using ChromaHint = std::function<Tensor(const Tensor &, const Settings &)>;
std::string graph_name(ScreenColor, Extent);
Tensors input_contract(Extent, bool modnet = false);
Tensors output_contract(Extent, bool modnet = false);
struct Graphs {
	std::unique_ptr<ck2::Graph> key, modnet;
	std::unique_ptr<ck2::SequenceState> stage1;
	ChromaHint chroma;
};
class SequenceState {
public:
	SequenceState(const Settings &, Graphs);
	ck2::LowResolutionResult process(const InputFrame &, std::stop_token = {},
					 const std::atomic<bool> *cancel = nullptr);
	void reset();

private:
	Extent grid_;
	ScreenColor screen_;
	HintSource hint_;
	Tensor hint_contract_;
	Tensors outputs_;
	Graphs graphs_;
};
} // namespace corridorkey::ck1
