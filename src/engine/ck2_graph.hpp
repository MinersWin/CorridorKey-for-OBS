// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "contracts/frame.hpp"
#include "providers/nvidia.hpp"
#include "providers/directml.hpp"
#include <map>
#include <string>

namespace corridorkey::ck2 {
struct Tensor {
	std::vector<std::int64_t> shape;
	std::vector<float> values;
	std::shared_ptr<nvidia::Buffer> device;
	std::shared_ptr<dml::Buffer> directml;
	Tensor() = default;
	explicit Tensor(std::vector<std::int64_t> dimensions, float fill = 0, bool host_storage = true);
};
// Device images contain planar model tensors. Their memory is never interpreted as a texture.
class DeviceImage final : public ImageStorage {
public:
	explicit DeviceImage(Tensor tensor) : tensor(std::move(tensor)) {}
	MemoryDomain domain() const noexcept override
	{
		return tensor.directml ? MemoryDomain::d3d12 : MemoryDomain::cuda;
	}
	std::uintptr_t native_handle() const noexcept override
	{
		return tensor.directml ? reinterpret_cast<std::uintptr_t>(tensor.directml->resource())
				       : static_cast<std::uintptr_t>(tensor.device->pointer());
	}
	Tensor tensor;
};
Tensor download_tensor(const Tensor &);
void zero_tensor(Tensor &);
Tensor device_rgb(const std::shared_ptr<nvidia::Buffer> &rgba, Extent, unsigned format,
		  std::shared_ptr<nvidia::Buffer> reuse = {});
using Tensors = std::map<std::string, Tensor>;
using Inputs = std::map<std::string, const Tensor *>;
enum class Stage { moge, rvm, stage1, stage2, refiner };
std::string graph_name(Stage, Extent);
Tensors input_contract(Stage, Extent);
Tensors output_contract(Stage, Extent);
void check_tensor(const Tensor &, const Tensor &, bool unit_range = false);
class Graph {
public:
	virtual ~Graph() = default;
	virtual Tensors run(const Inputs &, std::stop_token) = 0;
	virtual void reset_transform() {}
	virtual std::shared_ptr<nvidia::Context> device_context() const { return {}; }
	virtual std::shared_ptr<dml::Context> dml_context() const { return {}; }
	virtual double gpu_ms() const { return -1; }
};
using Graphs = std::map<Stage, std::unique_ptr<Graph>>;
Tensor planar_rgb(const Image &);
Tensor resize_area(const Tensor &, Extent, std::shared_ptr<nvidia::Buffer> reuse = {},
		   std::shared_ptr<dml::Buffer> dml_reuse = {});
Tensor resize_bilinear(const Tensor &, Extent);
Image packed_image(const Tensor &, PixelFormat);

struct StageTimes {
	double moge_ms = 0, rvm_ms = 0, stage1_ms = 0, stage2_ms = 0;
	bool moge_refreshed = false;
};
// W08 consumes decoded, straight sRGB foreground and linear alpha in sampled_roi.
// W13 refines the native reconstruction before final despill and levels.
struct LowResolutionResult {
	Tensor alpha, foreground;
	FrameTransform transform;
	Crop next_roi;
	StageTimes timings;
};
} // namespace corridorkey::ck2
