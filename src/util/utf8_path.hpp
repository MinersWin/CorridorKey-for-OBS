// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace corridorkey {

// Builds a path from UTF-8 text on every platform. std::filesystem::u8path is
// deprecated in C++20, and OBS builds MSVC without char8_t, so the portable
// path(const char8_t *) constructor isn't available there.
inline std::filesystem::path utf8_path(std::string_view text)
{
#ifdef _WIN32
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
	return std::filesystem::u8path(text.begin(), text.end());
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#else
	// POSIX paths are byte strings, and everything we pass in is UTF-8.
	return std::filesystem::path(std::string(text));
#endif
}

} // namespace corridorkey
