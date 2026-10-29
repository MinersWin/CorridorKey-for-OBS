// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "tensor_storage.hpp"
#include <algorithm>
#include <limits>

namespace corridorkey::models::storage {
std::uint64_t multiply(std::uint64_t a, std::uint64_t b)
{
	require(!a || b <= std::numeric_limits<std::uint64_t>::max() / a, "Tensor size overflow");
	return a * b;
}
std::uint64_t elements(const std::vector<std::uint64_t> &dims)
{
	require(dims.size() <= max_rank, "Tensor rank limit exceeded");
	std::uint64_t n = 1;
	for (auto d : dims) {
		require(d <= max_tensor, "Tensor dimension limit exceeded");
		n = multiply(n, d);
	}
	require(n <= max_tensor, "Tensor element limit exceeded");
	return n;
}
std::vector<std::uint64_t> shape(const Json &j)
{
	require(j.is_array() && j.size() <= max_rank, "Invalid tensor shape");
	std::vector<std::uint64_t> result;
	for (const auto &d : j)
		result.push_back(uint64(d));
	(void)elements(result);
	return result;
}
std::size_t dtype_size(std::string_view dtype)
{
	if (dtype == "float32" || dtype == "int32")
		return 4;
	if (dtype == "float16")
		return 2;
	if (dtype == "int64")
		return 8;
	if (dtype == "uint8" || dtype == "bool")
		return 1;
	throw Failure(ErrorCode::invalid_input, "Unsupported tensor dtype");
}
void cancelled(std::stop_token stop)
{
	require(!stop.stop_requested(), "Weight binding cancelled", ErrorCode::cancelled);
}
std::uint64_t little(std::string_view s, std::size_t off, std::size_t n)
{
	require(n <= 8 && off <= s.size() && n <= s.size() - off, "Truncated binary header");
	std::uint64_t value = 0;
	for (std::size_t i = 0; i < n; ++i)
		value |= std::uint64_t(static_cast<unsigned char>(s[off + i])) << (i * 8);
	return value;
}
File::File(const fs::path &path) : in_(path, std::ios::binary), size(fs::file_size(path))
{
	require(bool(in_) && fs::is_regular_file(path) && size <= max_file, "Missing or oversized tensor file");
}
void File::read(std::uint64_t offset, std::span<char> out)
{
	require(offset <= size && out.size() <= size - offset, "Tensor read outside file");
	in_.clear();
	in_.seekg(static_cast<std::streamoff>(offset));
	in_.read(out.data(), static_cast<std::streamsize>(out.size()));
	require(bool(in_), "Truncated tensor file", ErrorCode::io_failure);
}
std::string File::read(std::uint64_t off, std::size_t n)
{
	require(n <= max_header, "Read allocation limit exceeded");
	std::string out(n, '\0');
	read(off, std::span<char>(out));
	return out;
}
Safetensors::Safetensors(File &file)
{
	auto length = little(file.read(0, 8), 0, 8);
	require(length > 0 && length <= max_header && length % 8 == 0, "Invalid safetensors header size");
	auto bytes = file.read(8, static_cast<std::size_t>(length));
	require(bytes.front() == '{', "Invalid safetensors header");
	auto header = strict_json(bytes, max_header);
	require(header.is_object() && header.size() <= 100001, "Safetensors tensor count limit exceeded");
	const auto start = checked_add(8, length);
	std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
	for (const auto &[name, value] : header.items()) {
		if (name == "__metadata__") {
			require(value.is_object(), "Invalid safetensors metadata");
			for (const auto &v : value)
				require(v.is_string(), "Invalid safetensors metadata value");
			continue;
		}
		(void)string(Json(name));
		fields(value, {"dtype", "shape", "data_offsets"});
		Tensor t;
		auto dtype = string(value.at("dtype"));
		const std::map<std::string, std::string> types = {{"F32", "float32"}, {"F16", "float16"},
								  {"I32", "int32"},   {"I64", "int64"},
								  {"U8", "uint8"},    {"BOOL", "bool"}};
		require(types.contains(dtype), "Unsupported safetensors dtype");
		t.dtype = types.at(dtype);
		t.shape = shape(value.at("shape"));
		t.bytes = multiply(elements(t.shape), dtype_size(t.dtype));
		require(t.bytes <= max_tensor, "Tensor byte limit exceeded");
		auto &offsets = value.at("data_offsets");
		require(offsets.is_array() && offsets.size() == 2, "Invalid safetensors offsets");
		auto a = uint64(offsets.at(0)), b = uint64(offsets.at(1));
		require(b >= a && b - a == t.bytes && b <= file.size - start, "Invalid safetensors span");
		t.offset = checked_add(start, a);
		spans.emplace_back(a, b);
		tensors_.emplace(name, std::move(t));
	}
	std::sort(spans.begin(), spans.end());
	std::uint64_t end = 0;
	for (auto [a, b] : spans) {
		require(a == end, "Overlapping or incomplete safetensors data");
		end = b;
	}
	require(end == file.size - start, "Trailing safetensors data");
}
const Tensor &Safetensors::at(const std::string &name) const
{
	auto it = tensors_.find(name);
	require(it != tensors_.end(), "Missing safetensors key: " + name);
	return it->second;
}
} // namespace corridorkey::models::storage
