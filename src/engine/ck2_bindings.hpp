// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ort_session.hpp"
#include "ck2_graph.hpp"

namespace corridorkey::ck2 {
// The buffers and OrtValues keep their addresses for the complete session lifetime.
class PersistentBindings {
public:
	PersistentBindings(ort::Session &, bool cuda_device);
	~PersistentBindings();
	void set(std::string_view, std::span<const float>);
	void set(std::string_view, const Tensor &);
	Tensor device_output(std::string_view);
	std::shared_ptr<nvidia::Context> context() const;
	double gpu_ms() const { return gpu_ms_; }
	void run(std::stop_token = {});
	void read(std::string_view, std::span<float>);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	double gpu_ms_ = -1;
};
} // namespace corridorkey::ck2
