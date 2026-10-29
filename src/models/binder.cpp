// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "binder.hpp"
#include "tensor_storage.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace corridorkey::models {
namespace {
using namespace storage;
void sync_directory(const fs::path &path)
{
#ifndef _WIN32
	int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	require(fd >= 0, "Cannot open binding directory for sync", ErrorCode::io_failure);
	auto result = fsync(fd);
	close(fd);
	require(result == 0, "Binding directory sync failed", ErrorCode::io_failure);
#else
	(void)path; // Atomic publication uses MoveFileExW with MOVEFILE_WRITE_THROUGH.
#endif
}
void no_links(const fs::path &path)
{
	fs::path prefix;
	for (const auto &part : fs::absolute(path)) {
		prefix /= part;
		std::error_code ec;
		auto status = fs::symlink_status(prefix, ec);
		require(!ec || ec == std::errc::no_such_file_or_directory, "Cannot inspect binding path",
			ErrorCode::io_failure);
		require(!fs::is_symlink(status), "Symlink in binding path");
#ifdef _WIN32
		auto attrs = GetFileAttributesW(prefix.c_str());
		require(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
			"Reparse point in binding path");
#endif
	}
}
class Output {
	FILE *file_ = nullptr;

public:
	explicit Output(const fs::path &path)
	{
		no_links(path);
#ifdef _WIN32
		file_ = _wfopen(path.c_str(), L"wb");
#else
		file_ = fopen(path.c_str(), "wb");
#endif
		require(file_ != nullptr, "Cannot create binding output", ErrorCode::io_failure);
	}
	~Output()
	{
		if (file_)
			fclose(file_);
	}
	void write(std::span<const char> bytes)
	{
		require(fwrite(bytes.data(), 1, bytes.size(), file_) == bytes.size(),
			"Binding write failed (check free space)", ErrorCode::io_failure);
	}
	void finish()
	{
		require(fflush(file_) == 0, "Binding flush failed", ErrorCode::io_failure);
#ifdef _WIN32
		require(_commit(_fileno(file_)) == 0, "Binding sync failed", ErrorCode::io_failure);
#else
		require(fsync(fileno(file_)) == 0, "Binding sync failed", ErrorCode::io_failure);
#endif
		auto file = file_;
		file_ = nullptr;
		require(fclose(file) == 0, "Binding close failed", ErrorCode::io_failure);
	}
};
struct Cleanup {
	std::vector<fs::path> paths;
	~Cleanup()
	{
		for (auto &p : paths) {
			std::error_code ec;
			fs::remove(p, ec);
		}
	}
};
struct Source {
	File file;
	std::unique_ptr<Safetensors> safe;
	std::unique_ptr<Npz> npz;
	Source(const fs::path &path, const std::string &format, std::stop_token stop) : file(path)
	{
		if (format == "safetensors")
			safe = std::make_unique<Safetensors>(file);
		else {
			require(format == "npz", "Unsupported checkpoint format");
			npz = std::make_unique<Npz>(file, stop);
		}
	}
};
struct Row {
	std::string name, dtype;
	std::vector<std::uint64_t> shape;
	std::uint64_t offset, length;
	Json source;
};
struct Transpose {
	std::vector<std::uint64_t> output_shape, input_stride, perm;
	std::uint64_t backwards(std::uint64_t index) const
	{
		std::uint64_t result = 0;
		for (std::size_t i = output_shape.size(); i-- > 0;) {
			auto coord = index % output_shape[i];
			index /= output_shape[i];
			result += coord * input_stride[static_cast<std::size_t>(perm[i])];
		}
		return result;
	}
};
std::vector<Transpose> recipe(const Tensor &tensor, const Row &row)
{
	auto dims = tensor.shape;
	std::vector<Transpose> transforms;
	const auto &ops = row.source.at("ops");
	require(ops.is_array() && ops.size() <= 16, "Invalid operation count");
	for (const auto &op : ops) {
		require(op.is_array() && !op.empty(), "Invalid operation");
		auto kind = string(op.at(0));
		if (kind == "squeeze") {
			require(op.size() == 2, "Invalid squeeze arguments");
			auto axis = uint64(op.at(1));
			require(axis < dims.size() && dims[static_cast<std::size_t>(axis)] == 1,
				"Squeeze axis is not singleton");
			dims.erase(dims.begin() + static_cast<std::ptrdiff_t>(axis));
		} else if (kind == "merge") {
			require(op.size() == 3, "Invalid merge arguments");
			auto a = uint64(op.at(1)), b = uint64(op.at(2));
			require(a < b && b <= dims.size(), "Invalid merge axes");
			std::uint64_t n = 1;
			for (auto i = a; i < b; ++i)
				n = multiply(n, dims[static_cast<std::size_t>(i)]);
			dims[static_cast<std::size_t>(a)] = n;
			dims.erase(dims.begin() + static_cast<std::ptrdiff_t>(a + 1),
				   dims.begin() + static_cast<std::ptrdiff_t>(b));
		} else if (kind == "transpose") {
			require(op.size() == 2 && op.at(1).is_array() && op.at(1).size() == dims.size(),
				"Invalid transpose rank");
			Transpose t;
			t.input_stride.resize(dims.size());
			std::uint64_t stride = 1;
			for (std::size_t i = dims.size(); i-- > 0;) {
				t.input_stride[i] = stride;
				stride = multiply(stride, dims[i]);
			}
			std::set<std::uint64_t> seen;
			for (auto &p : op.at(1)) {
				auto axis = uint64(p);
				require(axis < dims.size() && seen.insert(axis).second,
					"Invalid transpose permutation");
				t.perm.push_back(axis);
				t.output_shape.push_back(dims[static_cast<std::size_t>(axis)]);
			}
			dims = t.output_shape;
			transforms.push_back(std::move(t));
		} else
			require(false, "Unknown operation");
	}
	require(dims == row.shape, "Transformed shape differs from manifest: " + row.name);
	return transforms;
}
// IEEE binary32 -> binary16, round-to-nearest ties-to-even, independent of host rounding mode.
std::uint16_t half(std::uint32_t bits)
{
	auto sign = (bits >> 16) & 0x8000u, exponent = (bits >> 23) & 255u, mantissa = bits & 0x7fffffu;
	if (exponent == 255)
		return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa ? ((mantissa >> 13) | 0x200u) : 0));
	if (exponent > 142)
		return static_cast<std::uint16_t>(sign | 0x7c00u);
	if (exponent < 102)
		return static_cast<std::uint16_t>(sign);
	unsigned shift = 13;
	std::uint32_t base = 0;
	if (exponent < 113) {
		shift = 126 - exponent;
		mantissa |= 0x800000u;
	} else
		base = (exponent - 112) << 10;
	auto rounded = mantissa >> shift, remainder = mantissa & ((1u << shift) - 1), midpoint = 1u << (shift - 1);
	if (remainder > midpoint || (remainder == midpoint && (rounded & 1)))
		++rounded;
	return static_cast<std::uint16_t>(sign | (base + rounded));
}
class Window {
	File &file_;
	const Tensor &tensor_;
	std::vector<char> bytes_;
	std::uint64_t start_ = 0;
	bool loaded_ = false;

public:
	Window(File &file, const Tensor &tensor)
		: file_(file),
		  tensor_(tensor),
		  bytes_(static_cast<std::size_t>(std::min<std::uint64_t>(tensor.bytes, 16 * 1024 * 1024)))
	{
	}
	const char *at(std::uint64_t offset, std::size_t width)
	{
		require(offset <= tensor_.bytes && width <= tensor_.bytes - offset, "Transform read outside tensor");
		if (!loaded_ || offset < start_ || offset + width > start_ + bytes_.size()) {
			start_ = std::min(offset, tensor_.bytes - bytes_.size());
			file_.read(tensor_.offset + start_, std::span<char>(bytes_));
			loaded_ = true;
		}
		return bytes_.data() + static_cast<std::size_t>(offset - start_);
	}
};
void materialize(File &file, const Tensor &tensor, const Row &row, Output &out,
		 const std::function<void(std::uint64_t)> &advance, std::stop_token stop)
{
	auto transforms = recipe(tensor, row);
	auto from_width = dtype_size(tensor.dtype), to_width = dtype_size(row.dtype);
	bool cast = tensor.dtype != row.dtype;
	require(!cast || (tensor.dtype == "float32" && row.dtype == "float16"), "Unsupported dtype conversion");
	std::vector<char> buffer(chunk_size);
	if (transforms.empty() && !cast) {
		for (std::uint64_t i = 0; i < row.length;) {
			cancelled(stop);
			auto n = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), row.length - i));
			file.read(tensor.offset + i, std::span<char>(buffer.data(), n));
			out.write({buffer.data(), n});
			i += n;
			advance(n);
		}
		return;
	}
	Window window(file, tensor);
	auto count = elements(row.shape);
	for (std::uint64_t i = 0; i < count;) {
		cancelled(stop);
		auto n = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size() / to_width, count - i));
		for (std::size_t j = 0; j < n; ++j) {
			auto source = i + j;
			for (auto t = transforms.rbegin(); t != transforms.rend(); ++t)
				source = t->backwards(source);
			const auto *data = window.at(source * from_width, from_width);
			auto *target = buffer.data() + j * to_width;
			if (cast) {
				auto value = half(static_cast<std::uint32_t>(little(std::string_view(data, 4), 0, 4)));
				target[0] = static_cast<char>(value & 255);
				target[1] = static_cast<char>(value >> 8);
			} else
				std::memcpy(target, data, to_width);
		}
		out.write({buffer.data(), n * to_width});
		i += n;
		advance(n * to_width);
	}
}
} // namespace

BindingResult bind_weights(std::string_view text, const std::map<std::string, fs::path> &paths,
			   const fs::path &output_path, std::string_view expected, std::stop_token stop,
			   BindingProgress progress)
{
	using namespace storage;
	auto output = fs::absolute(output_path).lexically_normal();
	require(output.extension() == ".weights", "Binding output must use .weights extension");
	auto partial = output;
	partial += ".partial";
	auto scratch = output;
	scratch += ".tensor.tmp";
	auto marker = output;
	marker += ".ready.json";
	auto receipt_tmp = marker;
	receipt_tmp += ".partial";
	auto lock_path = output;
	lock_path += ".lock";
	for (auto &p : {output, partial, scratch, marker, receipt_tmp, lock_path})
		no_links(p);
	fs::create_directories(output.parent_path());
	FileLock lock(lock_path, false, true, stop);
	fs::remove(marker);
	sync_directory(output.parent_path());
	Cleanup cleanup{{partial, scratch, receipt_tmp, marker}};
	cancelled(stop);
	auto manifest = strict_json(text, 32 * 1024 * 1024);
	fields(manifest, {"graph", "opset", "inputs", "outputs", "weights", "sources"}, {"H", "W", "weights_sha256"});
	require(uint64(manifest.at("opset")) == 18, "Unsupported exporter opset");
	require(string(manifest.at("graph")) == output.stem().string(), "Output basename differs from graph");
	std::string expected_hash(expected);
	if (manifest.contains("weights_sha256")) {
		auto pin = string(manifest.at("weights_sha256"));
		require(expected_hash.empty() || expected_hash == pin, "Conflicting output hashes");
		expected_hash = pin;
	}
	require(expected_hash.empty() || hex_string(expected_hash, 64), "Invalid expected output hash");
	const auto &weights = manifest.at("weights");
	require(weights.is_array() && weights.size() <= 100000, "Invalid weights table");
	std::vector<Row> rows;
	std::set<std::string> names;
	for (auto &w : weights) {
		fields(w, {"name", "dtype", "shape", "offset", "length", "source"});
		Row row{string(w.at("name")),   string(w.at("dtype")),  shape(w.at("shape")),
			uint64(w.at("offset")), uint64(w.at("length")), w.at("source")};
		require(names.insert(row.name).second && row.offset % 64 == 0, "Duplicate or unaligned weight");
		require(row.length == multiply(elements(row.shape), dtype_size(row.dtype)) &&
				row.length <= max_tensor && checked_add(row.offset, row.length) <= max_file,
			"Invalid output tensor span");
		fields(row.source, {"file", "key", "ops"});
		rows.push_back(std::move(row));
	}
	std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
		return std::pair{a.offset, a.length} < std::pair{b.offset, b.length};
	});
	std::uint64_t total = 0;
	for (auto &row : rows) {
		require(row.offset >= total, "Overlapping output tensors");
		total = row.offset + row.length;
	}
	if (progress)
		progress({0, total, true});
	auto free = fs::space(output.parent_path()).available;
	require(checked_add(total, std::uint64_t{256} << 20) <= free, "Insufficient binding workspace",
		ErrorCode::disk_full);
	auto &sources = manifest.at("sources");
	require(sources.is_object() && sources.size() <= 32, "Invalid source table");
	std::map<std::string, std::unique_ptr<Source>> readers;
	for (auto &[id, desc] : sources.items()) {
		cancelled(stop);
		require(desc.is_object() && desc.contains("format") && desc.contains("size") && desc.contains("sha256"),
			"Missing source pins");
		auto it = paths.find(id);
		require(it != paths.end(), "Missing local source: " + id, ErrorCode::model_missing);
		no_links(it->second);
		auto bytes = uint64(desc.at("size"));
		auto hash = string(desc.at("sha256"));
		require(bytes <= max_file && hex_string(hash, 64), "Invalid source pin");
		require(fs::file_size(it->second) == bytes && file_sha256(it->second, stop) == hash,
			"Source hash mismatch: " + id, ErrorCode::hash_mismatch);
		readers.emplace(id, std::make_unique<Source>(it->second, string(desc.at("format")), stop));
	}
	std::uint64_t done = 0;
	auto advance = [&](std::uint64_t n) {
		done += n;
		if (progress)
			progress({done, total, true});
	};
	{
		Output out(partial);
		std::vector<char> zero(chunk_size, '\0');
		auto zeros = [&](std::uint64_t n) {
			while (n) {
				cancelled(stop);
				auto take = static_cast<std::size_t>(std::min<std::uint64_t>(n, zero.size()));
				out.write({zero.data(), take});
				n -= take;
				advance(take);
			}
		};
		for (auto &row : rows) {
			cancelled(stop);
			zeros(row.offset - done);
			auto &src = row.source;
			if (src.at("file").is_null()) {
				auto &ops = src.at("ops");
				require(src.at("key").is_null() && ops.is_array() && ops.size() == 1 &&
						ops[0].is_array() &&
						((ops[0].size() == 1) || (ops[0].size() == 2 && ops[0][1].is_null())) &&
						ops[0][0] == "zeros",
					"Invalid zero source");
				zeros(row.length);
			} else {
				auto id = string(src.at("file"));
				auto key = string(src.at("key"));
				require(readers.contains(id), "Unknown source ID");
				auto &reader = *readers.at(id);
				if (reader.safe)
					materialize(reader.file, reader.safe->at(key), row, out, advance, stop);
				else {
					{
						Output tmp(scratch);
						reader.npz->extract(
							reader.npz->at(key),
							[&](std::span<const char> data) { tmp.write(data); }, stop);
						tmp.finish();
					}
					{
						File array(scratch);
						auto tensor = read_npy(array);
						materialize(array, tensor, row, out, advance, stop);
					}
					fs::remove(scratch);
				}
			}
		}
		out.finish();
	}
	cancelled(stop);
	require(fs::file_size(partial) == total, "Binding output size mismatch", ErrorCode::hash_mismatch);
	auto digest = file_sha256(partial, stop);
	require(expected_hash.empty() || digest == expected_hash, "Bound weights hash mismatch",
		ErrorCode::hash_mismatch);
	auto receipt = Json{
		{"schema_version", 1},
		{"binder_version", 1},
		{"manifest_sha256", sha256(text)},
		{"weights_sha256", digest},
		{"weights_bytes",
		 total}}.dump();
	{
		Output tmp(receipt_tmp);
		tmp.write(receipt);
		tmp.finish();
	}
	cancelled(stop);
	atomic_finalize(partial, output);
	sync_directory(output.parent_path());
	atomic_finalize(receipt_tmp, marker);
	sync_directory(output.parent_path());
	cleanup.paths.pop_back();
	return {total, digest};
}
void bind_package(const Manifest &m, Cache &cache, std::stop_token stop, BindingProgress progress)
{
	std::uint64_t total = 0, completed = 0;
	for (const auto &g : m.graphs)
		total = checked_add(total, g.weights_bytes);
	for (const auto &g : m.graphs) {
		storage::cancelled(stop);
		auto metadata =
			read_small_file(cache.staging(m.key) / m.artifacts.at(g.metadata).path, 32 * 1024 * 1024);
		validate_export_metadata(metadata, g, m);
		std::map<std::string, fs::path> sources;
		for (const auto &id : g.sources)
			sources.emplace(id, cache.blob(m.artifacts.at(id).sha256));
		auto result = bind_weights(metadata, sources, cache.staging(m.key) / g.external_data, g.weights_sha256,
					   stop, [&](Progress p) {
						   p.completed_bytes = checked_add(completed, p.completed_bytes);
						   p.total_bytes = total;
						   if (progress)
							   progress(p);
					   });
		require(result.bytes == g.weights_bytes, "Package bound size mismatch");
		completed += result.bytes;
	}
}
} // namespace corridorkey::models
