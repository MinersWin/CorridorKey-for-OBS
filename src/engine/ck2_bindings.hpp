// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ort_session.hpp"

namespace corridorkey::ck2 {
// The buffers and OrtValues keep their addresses for the complete session lifetime.
class PersistentBindings {
public:
	PersistentBindings(ort::Session &, bool cuda_device);
	~PersistentBindings();
	void set(std::string_view, std::span<const float>);
	void run(std::stop_token = {});
	void read(std::string_view, std::span<float>);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey::ck2
