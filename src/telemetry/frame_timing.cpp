// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "frame_timing.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace corridorkey {
namespace {
double ms(Duration value)
{
	return std::chrono::duration<double, std::milli>(value).count();
}
std::optional<double> percentile(std::vector<double> &values, double q)
{
	if (values.empty())
		return {};
	std::sort(values.begin(), values.end());
	return values[static_cast<std::size_t>(std::ceil(q * static_cast<double>(values.size()))) - 1];
}
} // namespace
void FrameTiming::record(const FrameResult &frame, TimePoint now, std::uint64_t tick) noexcept
{
	std::unique_lock lock(mutex_, std::try_to_lock);
	if (!lock.owns_lock() || frame.failure || now < frame.identity.captured_at)
		return;
	if (reset_requested_.exchange(false) || generation_ != frame.identity.generation) {
		count_ = next_ = 0;
		sequence_ = 0;
		tick_.reset();
		generation_ = frame.identity.generation;
	}
	if (tick_ == tick)
		return;
	tick_ = tick;
	const bool fresh = frame.identity.sequence != sequence_;
	sequence_ = frame.identity.sequence;
	const auto &t = frame.timings;
	samples_[next_] = {now,
			   ms(now - frame.identity.captured_at),
			   ms(t.inference),
			   ms(t.input_transfer),
			   ms(t.output_transfer),
			   std::max(0.0, ms(t.submitted_at - frame.identity.captured_at - t.input_transfer)),
			   fresh};
	next_ = (next_ + 1) % samples_.size();
	count_ = std::min(count_ + 1, samples_.size());
}
TimingSnapshot FrameTiming::snapshot(TimePoint now) const
{
	std::array<Sample, 1024> copy;
	std::size_t count;
	{
		std::lock_guard lock(mutex_);
		if (reset_requested_)
			return {};
		copy = samples_;
		count = count_;
	}
	TimingSnapshot result;
	std::vector<double> age, inference, input, output, queue;
	std::uint64_t fresh = 0;
	for (std::size_t i = 0; i < count; ++i) {
		const auto &s = copy[i];
		if (s.at > now || now - s.at > std::chrono::seconds(5))
			continue;
		if (!result.presentations || s.at < result.window_start)
			result.window_start = s.at;
		result.last_presented = std::max(result.last_presented, s.at);
		++result.presentations;
		age.push_back(s.age);
		if (s.fresh) {
			++fresh;
			inference.push_back(s.inference);
			input.push_back(s.input);
			output.push_back(s.output);
			queue.push_back(s.queue);
		} else
			++result.repeated;
	}
	result.age_median_ms = percentile(age, .5);
	result.age_p95_ms = percentile(age, .95);
	result.inference_median_ms = percentile(inference, .5);
	result.input_transfer_median_ms = percentile(input, .5);
	result.output_transfer_median_ms = percentile(output, .5);
	result.queue_median_ms = percentile(queue, .5);
	const auto seconds = std::chrono::duration<double>(result.last_presented - result.window_start).count();
	if (seconds > 0)
		result.keyed_fps = static_cast<double>(fresh > 0 ? fresh - 1 : 0) / seconds;
	return result;
}
void FrameTiming::reset() noexcept
{
	reset_requested_ = true;
}
} // namespace corridorkey
