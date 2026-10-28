// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include "settings.hpp"
#include "runtime/frame_pool.hpp"
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace corridorkey {
enum class PixelFormat { rgb_f32, rgba_f32, alpha_f32, rgba8, bgra8 };

// A resource's deleter must enqueue thread-affine destruction on its owner.
class ImageStorage {
public:
	virtual ~ImageStorage() = default;
	virtual MemoryDomain domain() const noexcept = 0;
	virtual std::span<const float> host_floats() const noexcept { return {}; }
	virtual std::uintptr_t native_handle() const noexcept { return 0; }
};
class HostFloats final : public ImageStorage {
public:
	explicit HostFloats(std::vector<float> values) : values_(std::move(values)) {}
	MemoryDomain domain() const noexcept override { return MemoryDomain::host; }
	std::span<const float> host_floats() const noexcept override { return values_; }

private:
	std::vector<float> values_;
};
struct Image {
	Extent size;
	PixelFormat format = PixelFormat::rgb_f32;
	ColorSpace color = ColorSpace::srgb;
	AlphaMode alpha = AlphaMode::opaque;
	std::size_t row_stride_bytes = 0;
	std::shared_ptr<const ImageStorage> storage;
};
class Completion {
public:
	virtual ~Completion() = default;
	// A zero-timeout query only. Once true, stays true. False keeps resources pinned.
	virtual bool ready() const noexcept = 0;
};

struct FrameIdentity {
	InstanceId instance = 0;
	Generation generation = 0;
	Sequence sequence = 0;
	std::uint64_t video_tick = 0; // Deduplication key, never a camera timestamp.
	TimePoint captured_at;
	bool operator==(const FrameIdentity &) const = default;
};
struct FrameTransform {
	Extent source_size;
	Crop user_crop;
	Crop sampled_roi;
	bool preserve_aspect = false;
	bool operator==(const FrameTransform &) const = default;
};
// W09 owns the tensors. This key identifies the per-instance temporal sequence.
struct TemporalStateKey {
	InstanceId instance = 0;
	Generation generation = 0;
	Sequence last_accepted = 0;
	Extent grid;
	ColorSpace source_color = ColorSpace::srgb;
	FrameTransform transform;
	TimePoint last_captured_at;
};
struct InputFrame {
	FrameLease lease;
	FrameIdentity identity;
	FrameTransform transform;
	Image original; // Retained until paired output has finished presentation.
	Image analysis;
	SettingsSnapshot settings;
	std::shared_ptr<const Completion> capture_complete; // Null means already ready.
	std::stop_token cancellation;
};
struct FrameTimings {
	TimePoint submitted_at, started_at, completed_at;
	Duration input_transfer{}, inference{}, refinement{}, output_transfer{};
};
struct FrameResult {
	InputFrame input; // Ownership travels with the result, including failed results.
	FrameIdentity identity;
	FrameTransform transform;
	Image foreground; // Straight sRGB RGB; alpha is a separate linear plane.
	Image alpha;
	FrameTimings timings;
	std::shared_ptr<const Completion> output_complete;
	Error failure;
};

Error validate_input(const InputFrame &input) noexcept;
// Host numeric validation belongs on the worker. Device implementations validate
// before publication; render consumers only check identity/fences/age.
Error validate_result(const FrameResult &result) noexcept;
bool presentable(const FrameResult &result, InstanceId instance, Generation generation, Sequence last_presented,
		 TimePoint now, Duration maximum_age) noexcept;
} // namespace corridorkey
