// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "tensor_storage.hpp"
#include <charconv>

namespace corridorkey::models::storage {
namespace {
// Parse only the three literal fields of an NPY header. No Python evaluation.
class Header {
	std::string_view text_;
	std::size_t at_ = 0;

public:
	explicit Header(std::string_view text) : text_(text) {}
	void spaces()
	{
		while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n'))
			++at_;
	}
	bool take(char c)
	{
		spaces();
		if (at_ == text_.size() || text_[at_] != c)
			return false;
		++at_;
		return true;
	}
	void expect(char c) { require(take(c), "Invalid NPY header syntax"); }
	std::string quoted()
	{
		spaces();
		require(at_ < text_.size() && (text_[at_] == '\'' || text_[at_] == '"'), "Invalid NPY string");
		auto quote = text_[at_++];
		auto start = at_;
		while (at_ < text_.size() && text_[at_] != quote) {
			require(text_[at_] >= ' ' && text_[at_] <= '~' && text_[at_] != '\\',
				"Unsupported NPY string escape");
			++at_;
		}
		require(at_ < text_.size(), "Unterminated NPY string");
		auto s = std::string(text_.substr(start, at_ - start));
		++at_;
		return s;
	}
	void false_literal()
	{
		spaces();
		require(text_.substr(at_, 5) == "False", "Fortran-order NPY arrays are unsupported");
		at_ += 5;
	}
	std::vector<std::uint64_t> dimensions()
	{
		expect('(');
		std::vector<std::uint64_t> dims;
		if (take(')'))
			return dims;
		for (;;) {
			spaces();
			std::uint64_t n = 0;
			auto begin = text_.data() + at_, end = text_.data() + text_.size();
			auto parsed = std::from_chars(begin, end, n);
			require(parsed.ec == std::errc{} && parsed.ptr != begin, "Invalid NPY dimension");
			at_ += static_cast<std::size_t>(parsed.ptr - begin);
			dims.push_back(n);
			require(dims.size() <= max_rank, "NPY rank limit exceeded");
			if (take(')')) {
				require(dims.size() > 1, "NPY singleton shape needs a tuple comma");
				break;
			}
			expect(',');
			if (take(')'))
				break;
		}
		return dims;
	}
	void finish()
	{
		spaces();
		require(at_ == text_.size(), "Trailing NPY header expressions");
	}
};
} // namespace
Tensor read_npy(File &file)
{
	auto prefix = file.read(0, 8);
	require(prefix.substr(0, 6) == std::string("\x93NUMPY", 6) && prefix[7] == 0, "Invalid NPY magic/version");
	unsigned version = static_cast<unsigned char>(prefix[6]);
	require(version >= 1 && version <= 3, "Unsupported NPY version");
	std::size_t width = version == 1 ? 2 : 4;
	auto length = little(file.read(8, width), 0, width);
	require(length > 0 && length <= 65536, "NPY header limit exceeded");
	auto text = file.read(8 + width, static_cast<std::size_t>(length));
	require(text.back() == '\n', "NPY header needs final newline");
	Header header(text);
	Tensor t;
	std::set<std::string> fields_seen;
	header.expect('{');
	for (;;) {
		auto key = header.quoted();
		require(fields_seen.insert(key).second, "Duplicate NPY field");
		header.expect(':');
		if (key == "descr") {
			auto dtype = header.quoted();
			const std::map<std::string, std::string> types = {{"<f4", "float32"}, {"<f2", "float16"},
									  {"<i4", "int32"},   {"<i8", "int64"},
									  {"|u1", "uint8"},   {"|b1", "bool"}};
			require(types.contains(dtype), "Unsupported NPY dtype or byte order (objects forbidden)");
			t.dtype = types.at(dtype);
		} else if (key == "fortran_order")
			header.false_literal();
		else if (key == "shape")
			t.shape = header.dimensions();
		else
			require(false, "Unknown NPY field");
		if (header.take('}'))
			break;
		header.expect(',');
		if (header.take('}'))
			break;
	}
	header.finish();
	require(fields_seen.size() == 3, "Missing NPY field");
	t.offset = 8 + width + length;
	require(t.offset % 16 == 0, "Invalid NPY header alignment");
	t.bytes = multiply(elements(t.shape), dtype_size(t.dtype));
	require(t.bytes <= max_tensor && t.offset <= file.size && t.bytes == file.size - t.offset,
		"NPY data size mismatch");
	return t;
}
} // namespace corridorkey::models::storage
