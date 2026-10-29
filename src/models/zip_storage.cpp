// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "tensor_storage.hpp"
#include <algorithm>
#include <array>
#include <zlib.h>

namespace corridorkey::models::storage {
namespace {
constexpr std::uint64_t max_member = std::uint64_t{256} << 20;
constexpr std::uint64_t max_expanded = std::uint64_t{8} << 30;
void zip64(std::string_view extra, std::uint64_t &bytes, std::uint64_t &compressed, std::uint64_t &offset)
{
	bool seen = false;
	for (std::size_t k = 0; k < extra.size();) {
		auto tag = little(extra, k, 2), n = little(extra, k + 2, 2);
		require(n <= extra.size() - k - 4, "Invalid ZIP extra field");
		if (tag == 1) {
			require(!seen, "Duplicate ZIP64 extra field");
			seen = true;
			auto z = extra.substr(k + 4, static_cast<std::size_t>(n));
			std::size_t at = 0;
			for (auto v : {&bytes, &compressed, &offset})
				if (*v == 0xffffffff) {
					*v = little(z, at, 8);
					at += 8;
				}
		}
		k += static_cast<std::size_t>(4 + n);
	}
	require((bytes != 0xffffffff && compressed != 0xffffffff && offset != 0xffffffff), "Missing ZIP64 sizes");
}
} // namespace
Npz::Npz(File &file, std::stop_token stop) : file_(file)
{
	require(file.size >= 22, "Invalid NPZ archive");
	auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(file.size, 65557));
	auto tail = file.read(file.size - tail_size, tail_size);
	std::size_t pos = tail.size() - 22;
	for (;;) {
		if (little(tail, pos, 4) == 0x06054b50 && pos + 22 + little(tail, pos + 20, 2) == tail.size())
			break;
		require(pos > 0, "Missing ZIP directory");
		--pos;
	}
	require(little(tail, pos + 4, 2) == 0 && little(tail, pos + 6, 2) == 0 &&
			little(tail, pos + 8, 2) == little(tail, pos + 10, 2),
		"Split ZIP forbidden");
	auto count = little(tail, pos + 10, 2), length = little(tail, pos + 12, 4), offset = little(tail, pos + 16, 4);
	auto directory_end = file.size - tail_size + pos;
	if (count == 65535 || length == 0xffffffff || offset == 0xffffffff) {
		require(directory_end >= 20, "Missing ZIP64 locator");
		auto locator = file.read(directory_end - 20, 20);
		require(little(locator, 0, 4) == 0x07064b50 && little(locator, 4, 4) == 0 &&
				little(locator, 16, 4) == 1,
			"Invalid ZIP64 locator");
		auto zip64_offset = little(locator, 8, 8);
		auto header = file.read(zip64_offset, 56);
		require(little(header, 0, 4) == 0x06064b50 && little(header, 4, 8) >= 44 &&
				checked_add(zip64_offset, checked_add(12, little(header, 4, 8))) ==
					directory_end - 20 &&
				little(header, 16, 4) == 0 && little(header, 20, 4) == 0 &&
				little(header, 24, 8) == little(header, 32, 8),
			"Invalid ZIP64 directory");
		count = little(header, 32, 8);
		length = little(header, 40, 8);
		offset = little(header, 48, 8);
		directory_end = zip64_offset;
	}
	require(count <= 10000 && length <= 8 * 1024 * 1024 && checked_add(offset, length) == directory_end,
		"ZIP directory limits or extent invalid");
	auto directory = file.read(offset, static_cast<std::size_t>(length));
	std::size_t cursor = 0;
	std::uint64_t expanded = 0;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
	for (std::uint64_t i = 0; i < count; ++i) {
		cancelled(stop);
		require(little(directory, cursor, 4) == 0x02014b50, "Invalid ZIP directory entry");
		auto nl = little(directory, cursor + 28, 2), xl = little(directory, cursor + 30, 2),
		     cl = little(directory, cursor + 32, 2);
		auto extent = checked_add(46, checked_add(nl, checked_add(xl, cl)));
		require(extent <= directory.size() - cursor, "Truncated ZIP directory entry");
		ZipEntry e;
		e.name = directory.substr(cursor + 46, static_cast<std::size_t>(nl));
		relative_path(e.name);
		require(e.name.ends_with(".npy"), "NPZ contains a non-NPY member");
		auto flags = little(directory, cursor + 8, 2);
		e.method = static_cast<unsigned>(little(directory, cursor + 10, 2));
		require((flags & ~std::uint64_t{0x080e}) == 0 && (e.method == 0 || e.method == 8) &&
				(e.method == 8 || !(flags & 6)) && little(directory, cursor + 34, 2) == 0,
			"Unsupported, split or encrypted ZIP member");
		auto attrs = little(directory, cursor + 38, 4), mode = attrs >> 16;
		require(((mode & 0170000) == 0 || (mode & 0170000) == 0100000) && !(attrs & 0x10),
			"ZIP links/devices/directories forbidden");
		e.crc = static_cast<std::uint32_t>(little(directory, cursor + 16, 4));
		e.compressed = little(directory, cursor + 20, 4);
		e.bytes = little(directory, cursor + 24, 4);
		auto local_offset = little(directory, cursor + 42, 4);
		zip64(std::string_view(directory).substr(cursor + 46 + static_cast<std::size_t>(nl),
							 static_cast<std::size_t>(xl)),
		      e.bytes, e.compressed, local_offset);
		expanded = checked_add(expanded, e.bytes);
		require(e.bytes <= max_member && expanded <= max_expanded &&
				e.bytes <= multiply(std::max<std::uint64_t>(1, e.compressed), 1000),
			"NPZ expansion limit exceeded");
		require(e.method != 0 || e.bytes == e.compressed, "Stored ZIP size mismatch");
		auto local = file.read(local_offset, 30);
		require(little(local, 0, 4) == 0x04034b50 && little(local, 6, 2) == flags &&
				little(local, 8, 2) == e.method,
			"ZIP local header mismatch");
		auto local_nl = little(local, 26, 2), local_xl = little(local, 28, 2);
		require(file.read(checked_add(local_offset, 30), static_cast<std::size_t>(local_nl)) == e.name,
			"ZIP local name mismatch");
		e.start = checked_add(local_offset, checked_add(30, checked_add(local_nl, local_xl)));
		require(checked_add(e.start, e.compressed) <= offset, "ZIP data overlaps directory");
		auto local_bytes = little(local, 22, 4), local_compressed = little(local, 18, 4);
		std::uint64_t unused_offset = 0;
		zip64(file.read(checked_add(local_offset, 30 + local_nl), static_cast<std::size_t>(local_xl)),
		      local_bytes, local_compressed, unused_offset);
		if (!(flags & 8))
			require(local_bytes == e.bytes && local_compressed == e.compressed &&
					little(local, 14, 4) == e.crc,
				"ZIP local sizes/CRC mismatch");
		auto end = e.start + e.compressed;
		if (flags & 8) {
			auto descriptor = file.read(end, 4);
			if (little(descriptor, 0, 4) == 0x08074b50)
				end = checked_add(end, 4);
			auto width = (little(local, 18, 4) == 0xffffffff || little(local, 22, 4) == 0xffffffff ||
				      e.bytes >= 0xffffffff || e.compressed >= 0xffffffff)
					     ? std::size_t{8}
					     : std::size_t{4};
			descriptor = file.read(end, 4 + width * 2);
			require(little(descriptor, 0, 4) == e.crc && little(descriptor, 4, width) == e.compressed &&
					little(descriptor, 4 + width, width) == e.bytes,
				"ZIP data descriptor mismatch");
			end = checked_add(end, 4 + width * 2);
			require(end <= offset, "ZIP descriptor overlaps directory");
		}
		spans.emplace_back(local_offset, end);
		auto key = e.name.substr(0, e.name.size() - 4);
		require(entries_.emplace(key, std::move(e)).second, "Duplicate NPZ key");
		cursor += static_cast<std::size_t>(extent);
	}
	require(cursor == directory.size(), "Trailing ZIP directory data");
	std::sort(spans.begin(), spans.end());
	std::uint64_t end = 0;
	for (auto [a, b] : spans) {
		require(a >= end, "Overlapping ZIP members");
		end = b;
	}
}
const ZipEntry &Npz::at(const std::string &key) const
{
	auto it = entries_.find(key);
	require(it != entries_.end(), "Missing NPZ key: " + key);
	return it->second;
}
void Npz::extract(const ZipEntry &e, const std::function<void(std::span<const char>)> &sink, std::stop_token stop)
{
	std::uint64_t consumed = 0, emitted = 0;
	uLong crc = crc32(0, Z_NULL, 0);
	auto emit = [&](std::span<const char> bytes) {
		require(bytes.size() <= e.bytes - emitted, "ZIP expansion exceeds declared size");
		crc = crc32(crc, reinterpret_cast<const Bytef *>(bytes.data()), static_cast<uInt>(bytes.size()));
		sink(bytes);
		emitted += bytes.size();
	};
	if (e.method == 0) {
		while (consumed < e.compressed) {
			cancelled(stop);
			auto n = static_cast<std::size_t>(std::min<std::uint64_t>(65536, e.compressed - consumed));
			auto bytes = file_.read(e.start + consumed, n);
			emit(bytes);
			consumed += n;
		}
	} else {
		z_stream z{};
		require(inflateInit2(&z, -MAX_WBITS) == Z_OK, "Cannot initialize ZIP inflater", ErrorCode::internal);
		struct End {
			z_stream *z;
			~End() { inflateEnd(z); }
		} cleanup{&z};
		std::array<char, 65536> output{};
		std::string input;
		int result = Z_OK;
		while (result != Z_STREAM_END) {
			cancelled(stop);
			if (!z.avail_in && consumed < e.compressed) {
				auto n = static_cast<std::size_t>(
					std::min<std::uint64_t>(65536, e.compressed - consumed));
				input = file_.read(e.start + consumed, n);
				consumed += n;
				z.next_in = reinterpret_cast<Bytef *>(input.data());
				z.avail_in = static_cast<uInt>(n);
			}
			z.next_out = reinterpret_cast<Bytef *>(output.data());
			z.avail_out = static_cast<uInt>(output.size());
			auto before = z.avail_in;
			result = inflate(&z, Z_NO_FLUSH);
			require(result == Z_OK || result == Z_STREAM_END, "Invalid deflate stream");
			auto n = output.size() - z.avail_out;
			emit(std::span<const char>(output.data(), n));
			require(result == Z_STREAM_END || n || z.avail_in < before, "Truncated deflate stream");
		}
		require(consumed == e.compressed && z.avail_in == 0, "Trailing deflate bytes");
	}
	require(emitted == e.bytes && static_cast<std::uint32_t>(crc) == e.crc, "ZIP size/CRC verification failed",
		ErrorCode::hash_mismatch);
}
} // namespace corridorkey::models::storage
