// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pipeline.hpp"
#include "ck2_graph.hpp"
#include "refiner_stage.hpp"

namespace corridorkey {
class ModelPipeline : public Pipeline {
public:
	virtual std::shared_ptr<nvidia::Context> device_context() const { return {}; }
	virtual std::shared_ptr<dml::Context> dml_context() const { return {}; }
	virtual Crop suggested_roi() const = 0;
	virtual ck2::StageTimes stage_times() const = 0;
	virtual std::string detail() const = 0;
	virtual std::shared_ptr<refiner::Stage> refiner_stage() const = 0;
};
} // namespace corridorkey
