// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "manifest.hpp"
#include <fstream>
#include <functional>
#include <span>

namespace corridorkey::models::storage {
inline constexpr std::uint64_t max_file = std::uint64_t{16} << 30;
inline constexpr std::uint64_t max_tensor = std::uint64_t{4} << 30;
inline constexpr std::size_t max_header = 16 * 1024 * 1024;
inline constexpr std::size_t max_rank = 8;
inline constexpr std::size_t chunk_size = 1024 * 1024;
std::uint64_t multiply(std::uint64_t, std::uint64_t);
std::uint64_t elements(const std::vector<std::uint64_t> &);
std::vector<std::uint64_t> shape(const Json &);
std::size_t dtype_size(std::string_view);
void cancelled(std::stop_token);
std::uint64_t little(std::string_view, std::size_t, std::size_t);

class File {
	std::ifstream in_;

public:
	const std::uint64_t size;
	explicit File(const fs::path &);
	void read(std::uint64_t offset, std::span<char>);
	std::string read(std::uint64_t offset, std::size_t length);
};
struct Tensor {
	std::string dtype;
	std::vector<std::uint64_t> shape;
	std::uint64_t offset = 0, bytes = 0;
};
class Safetensors {
	std::map<std::string, Tensor> tensors_;

public:
	explicit Safetensors(File &);
	const Tensor &at(const std::string &) const;
};
Tensor read_npy(File &);

struct ZipEntry {
	std::string name;
	std::uint64_t start = 0, compressed = 0, bytes = 0;
	unsigned method = 0;
	std::uint32_t crc = 0;
};
class Npz {
	File &file_;
	std::map<std::string, ZipEntry> entries_;

public:
	explicit Npz(File &, std::stop_token = {});
	const ZipEntry &at(const std::string &) const;
	void extract(const ZipEntry &, const std::function<void(std::span<const char>)> &, std::stop_token);
};
} // namespace corridorkey::models::storage
