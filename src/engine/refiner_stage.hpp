// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ck2_graph.hpp"
#include "contracts/models.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace corridorkey::refiner {
inline constexpr unsigned tile = 32, halo = 10, patch = 52, channels = 11, atlas_columns = 16;
bool supported_budget(unsigned budget) noexcept;
Extent input_atlas(unsigned budget);
Extent output_atlas(unsigned budget);
// Three vertically stacked RGBA planes: I.rgb/B.r, B.gb/F.rg, F.b/a_up/a_gf/unused.
ck2::Tensor unpack_atlas(std::span<const float> rgba, unsigned budget);
std::vector<float> pack_centres(const ck2::Tensors &, unsigned budget);
struct Result {
	std::vector<float> rgba;
	std::string failure;
	double inference_ms = 0;
	double unpack_ms = 0, pack_ms = 0;
};
// One outstanding atlas, including an unconsumed result. The graph and device addresses stay fixed.
class Stage {
public:
	Stage(std::unique_ptr<ck2::Graph>, unsigned budget, std::shared_ptr<const ModelReadLease> lease = {});
	~Stage();
	Stage(const Stage &) = delete;
	Stage &operator=(const Stage &) = delete;
	bool submit(std::vector<float> &atlas);
	std::optional<Result> poll();
	unsigned budget() const noexcept { return budget_; }

private:
	void work(std::stop_token);
	std::shared_ptr<const ModelReadLease> lease_;
	std::unique_ptr<ck2::Graph> graph_;
	unsigned budget_;
	std::mutex mutex_;
	std::condition_variable wake_;
	std::optional<std::vector<float>> pending_;
	std::optional<Result> result_;
	bool busy_ = false;
	std::jthread worker_;
};
} // namespace corridorkey::refiner
