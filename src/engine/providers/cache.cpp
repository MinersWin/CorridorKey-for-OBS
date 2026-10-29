// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <fstream>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace corridorkey::ort {
namespace {
void copy_artifacts(const fs::path &source, const fs::path &target)
{
	for (const auto &entry : fs::recursive_directory_iterator(source))
		if (entry.is_symlink())
			throw std::runtime_error("Backend cache contains a symlink");
	fs::copy(source, target, fs::copy_options::recursive);
}
} // namespace
void CacheLease::lock()
{
#ifdef _WIN32
	const auto handle = CreateFileW((shared_ / "cache.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
					OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Backend cache busy");
	lock_ = reinterpret_cast<std::intptr_t>(handle);
#else
	lock_ = open((shared_ / "cache.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock_ < 0)
		throw std::runtime_error("Backend cache lock failed");
	if (flock(static_cast<int>(lock_), LOCK_EX | LOCK_NB) != 0) {
		unlock();
		throw std::runtime_error("Backend cache busy");
	}
#endif
}
void CacheLease::unlock() noexcept
{
	if (lock_ == -1)
		return;
#ifdef _WIN32
	CloseHandle(reinterpret_cast<HANDLE>(lock_));
#else
	close(static_cast<int>(lock_));
#endif
	lock_ = -1;
}
CacheLease::CacheLease(const fs::path &root, const CacheIdentity &identity, bool rebuild)
{
	const auto metadata = identity.canonical();
	const auto key = sha256(metadata);
	const auto absolute_root = fs::absolute(root);
	shared_ = absolute_root / key;
#ifdef _WIN32
	// Keep long engine filenames usable even when the host lacks a long-path manifest.
	auto native = shared_.make_preferred().native();
	if (!native.starts_with(L"\\\\?\\"))
		shared_ = fs::path(native.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + native.substr(2)
							       : L"\\\\?\\" + native);
#endif
	fs::create_directories(shared_);
	if (fs::is_symlink(shared_))
		throw std::runtime_error("Backend cache directory is a symlink");
	lock();
	try {
		const auto manifest = shared_ / "identity.json";
		bool valid = !rebuild && fs::exists(shared_ / "ready") && fs::is_directory(shared_ / "artifacts");
		if (fs::exists(manifest) && fs::file_size(manifest) < 1024 * 1024) {
			std::ifstream in(manifest);
			const std::string old((std::istreambuf_iterator<char>(in)), {});
			valid = valid && old == metadata;
		} else
			valid = false;
		if (!valid) {
			fs::remove(shared_ / "ready");
			fs::remove_all(shared_ / "artifacts");
			fs::remove_all(shared_ / "next");
			std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
			out << metadata;
			out.close();
			if (!out)
				throw std::runtime_error("Cannot write backend cache identity");
		}
		std::random_device random;
		for (int attempt = 0; attempt < 8; ++attempt) {
			// Provider cache filenames are long; the identity already belongs to shared_.
			path_ = absolute_root /
				(".working-" + std::to_string(random()) + "-" + std::to_string(random()));
			if (fs::create_directory(path_))
				break;
			path_.clear();
		}
		if (path_.empty())
			throw std::runtime_error("Cannot allocate private backend cache workspace");
		if (valid)
			copy_artifacts(shared_ / "artifacts", path_);
		unlock();
	} catch (...) {
		unlock();
		std::error_code ec;
		if (!path_.empty())
			fs::remove_all(path_, ec);
		throw;
	}
}
void CacheLease::commit()
{
	lock();
	try {
		// Readers hold the same lock. A crash before ready leaves no reusable entry.
		fs::remove(shared_ / "ready");
		fs::remove_all(shared_ / "next");
		copy_artifacts(path_, shared_ / "next");
		fs::remove_all(shared_ / "artifacts");
		fs::rename(shared_ / "next", shared_ / "artifacts");
		std::ofstream out(shared_ / "ready.tmp", std::ios::trunc);
		out << "1\n";
		out.close();
		if (!out)
			throw std::runtime_error("Cannot publish backend cache");
		fs::rename(shared_ / "ready.tmp", shared_ / "ready");
		unlock();
	} catch (...) {
		unlock();
		throw;
	}
}
CacheLease::~CacheLease()
{
	unlock();
	std::error_code ec;
	if (!path_.empty())
		fs::remove_all(path_, ec);
}
} // namespace corridorkey::ort
