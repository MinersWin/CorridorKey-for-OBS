// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck1_sequence.hpp"
#include <stdexcept>

namespace corridorkey::ck1 {
std::string graph_name(ScreenColor screen, Extent grid)
{
	if (grid.width != grid.height || (grid.width != 256 && grid.width != 384 && grid.width != 512))
		throw std::invalid_argument("CK1 requires a released square grid");
	return std::string(screen == ScreenColor::blue ? "ck1blue_" : "ck1green_") + std::to_string(grid.width);
}
Tensors input_contract(Extent grid, bool modnet)
{
	(void)graph_name(ScreenColor::green, grid);
	Tensors inputs{{"rgb", Tensor({1, 3, grid.height, grid.width})}};
	if (!modnet)
		inputs.emplace("hint", Tensor({1, 1, grid.height, grid.width}));
	return inputs;
}
Tensors output_contract(Extent grid, bool modnet)
{
	(void)graph_name(ScreenColor::green, grid);
	if (modnet)
		return {{"matte", Tensor({1, 1, grid.height, grid.width})}};
	return {{"alpha", Tensor({1, 1, grid.height, grid.width})}, {"fg", Tensor({1, 3, grid.height, grid.width})}};
}
SequenceState::SequenceState(const Settings &settings, Graphs graphs)
	: grid_(settings.quality.grid),
	  screen_(settings.quality.screen_color),
	  hint_(settings.hint.source),
	  hint_contract_(input_contract(grid_).at("hint")),
	  outputs_(output_contract(grid_)),
	  graphs_(std::move(graphs))
{
	(void)graph_name(screen_, grid_);
	if (!graphs_.key || (hint_ == HintSource::modnet && !graphs_.modnet) ||
	    (hint_ == HintSource::ck2_stage1 && !graphs_.stage1) || (hint_ == HintSource::chroma && !graphs_.chroma))
		throw std::invalid_argument("The selected CK1 hint provider is missing");
}
void SequenceState::reset()
{
	if (graphs_.stage1)
		graphs_.stage1->reset();
}
ck2::LowResolutionResult SequenceState::process(const InputFrame &input, std::stop_token stop,
						const std::atomic<bool> *cancel)
{
	if (!input.settings || input.analysis.size != grid_ || input.settings->quality.grid != grid_ ||
	    input.settings->quality.screen_color != screen_ || input.settings->hint.source != hint_ ||
	    input.transform.sampled_roi != input.transform.user_crop || input.transform.preserve_aspect)
		throw std::invalid_argument("CK1 frame differs from its prepared grid, screen, hint or crop");
	auto cancelled = [&] {
		if (stop.stop_requested() || input.cancellation.stop_requested() || (cancel && cancel->load()))
			throw std::runtime_error("CK1 cancelled between stages");
	};
	cancelled();
	auto rgb = ck2::planar_rgb(input.analysis);
	ck2::LowResolutionResult result;
	result.transform = input.transform;
	result.next_roi = input.transform.user_crop;
	const auto hint_start = Clock::now();
	Tensor hint;
	if (hint_ == HintSource::modnet) {
		auto out = graphs_.modnet->run({{"rgb", &rgb}}, stop);
		if (out.size() != 1)
			throw std::runtime_error("MODNet output count mismatch");
		hint = std::move(out.at("matte"));
	} else if (hint_ == HintSource::ck2_stage1) {
		// Reuse CK2's foundation/cache/state path on this capture, never another source's frame.
		auto settings = std::make_shared<Settings>(*input.settings);
		settings->quality.model = ModelFamily::ck2;
		settings->quality.grid = {512, 512};
		settings->quality.profile = Profile::custom;
		settings->quality.roi_enabled = false;
		settings->quality.refiner_enabled = false;
		settings->hint.ck2_mode = HintMode::off;
		InputFrame aligned;
		aligned.identity = input.identity;
		aligned.original = input.original;
		aligned.transform = input.transform;
		aligned.settings = settings;
		aligned.cancellation = input.cancellation;
		aligned.analysis = ck2::packed_image(ck2::resize_bilinear(rgb, {512, 512}), PixelFormat::rgb_f32);
		auto decoded = graphs_.stage1->process(aligned, stop, cancel);
		hint = ck2::resize_bilinear(decoded.alpha, grid_);
	} else if (hint_ == HintSource::chroma) {
		hint = graphs_.chroma(rgb, *input.settings);
	} else {
		throw std::invalid_argument("CK1 hint source has no evaluated provider");
	}
	ck2::check_tensor(hint, hint_contract_, true);
	const auto hint_ms = std::chrono::duration<double, std::milli>(Clock::now() - hint_start).count();
	cancelled();
	const auto key_start = Clock::now();
	auto decoded = graphs_.key->run({{"rgb", &rgb}, {"hint", &hint}}, stop);
	if (decoded.size() != outputs_.size())
		throw std::runtime_error("CK1 output count mismatch");
	for (const auto &[name, tensor] : outputs_)
		ck2::check_tensor(decoded.at(name), tensor, true);
	result.timings.stage1_ms = hint_ms;
	result.timings.stage2_ms = std::chrono::duration<double, std::milli>(Clock::now() - key_start).count();
	result.alpha = input.settings->context.output == OutputView::hint ? std::move(hint)
									  : std::move(decoded.at("alpha"));
	result.foreground = std::move(decoded.at("fg"));
	cancelled();
	return result;
}
} // namespace corridorkey::ck1
