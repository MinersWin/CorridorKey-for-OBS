// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace corridorkey {
using InstanceId = std::uint64_t;
using Generation = std::uint64_t;
using Sequence = std::uint64_t;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

struct Extent {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	bool operator==(const Extent &) const = default;
};

// Normalized absolute bounds in the original input, including right/bottom.
struct Crop {
	float left = 0, top = 0, right = 1, bottom = 1;
	bool operator==(const Crop &) const = default;
};

enum class ErrorCode : std::uint16_t {
	none,
	busy,
	cancelled,
	stale_generation,
	invalid_input,
	invalid_output,
	out_of_memory,
	backend_unavailable,
	device_lost,
	timeout,
	model_missing,
	licence_required,
	hash_mismatch,
	disk_full,
	io_failure,
	unsupported,
	internal,
};

// Static text keeps error reporting allocation-free, including OOM paths.
struct Error {
	ErrorCode code = ErrorCode::none;
	std::string_view action;
	explicit operator bool() const noexcept { return code != ErrorCode::none; }
};
Error error(ErrorCode code) noexcept;
std::string_view error_id(ErrorCode code) noexcept;

enum class Backend { automatic, tensorrt_rtx, tensorrt, cuda, directml, migraphx, cpu_diagnostic, fake };
enum class TransportKind { automatic, interop, staged_grid, staged_tiles, staged_full_frame, synthetic };
enum class Precision { automatic, fp16, fp32, bf16 };
enum class ColorSpace { srgb, rec709, linear_srgb, hdr_unsupported };
enum class AlphaMode { straight, premultiplied, opaque };
enum class MemoryDomain { host, d3d11, d3d12, cuda, opengl, hip };
enum class ResetReason {
	initial,
	source_changed,
	dimensions,
	grid,
	crop,
	color,
	seek,
	scene_cut,
	long_gap,
	explicit_reset,
	failure
};
} // namespace corridorkey
