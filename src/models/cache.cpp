// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "cache.hpp"
#include <array>
#include <atomic>
#include <fstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace corridorkey::models {
struct FileLock::Impl {
#ifdef _WIN32
	HANDLE handle = INVALID_HANDLE_VALUE;
	OVERLAPPED overlap{};
#else
	int handle = -1;
#endif
	bool held = false;
	~Impl()
	{
#ifdef _WIN32
		if (held)
			UnlockFileEx(handle, 0, 1, 0, &overlap);
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
#else
		if (held)
			flock(handle, LOCK_UN);
		if (handle != -1)
			close(handle);
#endif
	}
};
FileLock::FileLock(const fs::path &p, bool shared, bool wait, std::stop_token stop) : impl(std::make_unique<Impl>())
{
	fs::create_directories(p.parent_path());
#ifdef _WIN32
	impl->handle = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	require(impl->handle != INVALID_HANDLE_VALUE, "Cannot open cache lock", ErrorCode::io_failure);
#else
	impl->handle = open(p.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	require(impl->handle >= 0, "Cannot open cache lock", ErrorCode::io_failure);
#endif
	for (;;) {
		require(!stop.stop_requested(), "Cancelled while waiting for cache lock", ErrorCode::cancelled);
#ifdef _WIN32
		impl->held = LockFileEx(impl->handle,
					LOCKFILE_FAIL_IMMEDIATELY | (shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK), 0, 1, 0,
					&impl->overlap) != 0;
		if (!impl->held)
			require(GetLastError() == ERROR_LOCK_VIOLATION || GetLastError() == ERROR_IO_PENDING,
				"Cache lock failed", ErrorCode::io_failure);
#else
		impl->held = flock(impl->handle, (shared ? LOCK_SH : LOCK_EX) | LOCK_NB) == 0;
		if (!impl->held)
			require(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR, "Cache lock failed",
				ErrorCode::io_failure);
#endif
		if (impl->held || !wait)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
}
FileLock::~FileLock() = default;
FileLock::FileLock() noexcept = default;
FileLock::FileLock(FileLock &&) noexcept = default;
FileLock &FileLock::operator=(FileLock &&) noexcept = default;
bool FileLock::owns_lock() const noexcept
{
	return impl && impl->held;
}
std::string read_small_file(const fs::path &p, std::size_t limit)
{
	require(fs::is_regular_file(p) && fs::file_size(p) <= limit, "Metadata file missing or oversized",
		ErrorCode::io_failure);
#ifdef _WIN32
	// Readers pin the old file object while an atomic rename publishes its successor.
	HANDLE raw = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(raw != INVALID_HANDLE_VALUE, "Metadata open failed", ErrorCode::io_failure);
	struct Close {
		HANDLE handle;
		~Close() { CloseHandle(handle); }
	} close{raw};
	LARGE_INTEGER size{};
	require(GetFileSizeEx(raw, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= limit &&
			static_cast<std::uint64_t>(size.QuadPart) <= MAXDWORD,
		"Metadata size changed", ErrorCode::io_failure);
	std::string s(static_cast<std::size_t>(size.QuadPart), '\0');
	DWORD read = 0;
	require(ReadFile(raw, s.data(), static_cast<DWORD>(s.size()), &read, nullptr) && read == s.size(),
		"Metadata read failed", ErrorCode::io_failure);
#else
	std::ifstream in(p, std::ios::binary | std::ios::ate);
	auto size = in.tellg();
	require(bool(in) && size >= 0 && static_cast<std::uint64_t>(size) <= limit, "Metadata size changed",
		ErrorCode::io_failure);
	std::string s(static_cast<std::size_t>(size), '\0');
	in.seekg(0);
	in.read(s.data(), static_cast<std::streamsize>(s.size()));
	require(bool(in) && in.peek() == std::char_traits<char>::eof(), "Metadata read failed", ErrorCode::io_failure);
#endif
	return s;
}
void atomic_finalize(const fs::path &partial, const fs::path &destination)
{
#ifdef _WIN32
	for (unsigned attempt = 0;; ++attempt) {
		if (MoveFileExW(partial.c_str(), destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			break;
		auto reason = GetLastError();
		require(attempt < 20 && (reason == ERROR_SHARING_VIOLATION || reason == ERROR_ACCESS_DENIED),
			"Atomic rename failed", ErrorCode::io_failure);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
#else
	std::error_code ec;
	fs::rename(partial, destination, ec);
	require(!ec, "Atomic rename failed", ErrorCode::io_failure);
	int fd = open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd >= 0) {
		(void)fsync(fd);
		close(fd);
	}
#endif
}
void atomic_write(const fs::path &p, std::string_view bytes)
{
	static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
	auto pid = GetCurrentProcessId();
#else
	auto pid = getpid();
#endif
	auto tmp = p;
	tmp += ".tmp-" + std::to_string(pid) + "-" + std::to_string(sequence++);
	fs::create_directories(p.parent_path());
	try {
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		out.flush();
		require(bool(out), "Metadata write failed", ErrorCode::io_failure);
		out.close();
		atomic_finalize(tmp, p);
	} catch (...) {
		std::error_code ec;
		fs::remove(tmp, ec);
		throw;
	}
}
static void no_links(const fs::path &p)
{
	fs::path prefix;
	for (const auto &part : p) {
		prefix /= part;
		std::error_code ec;
		auto s = fs::symlink_status(prefix, ec);
		if (ec && ec != std::errc::no_such_file_or_directory)
			throw Failure(ErrorCode::io_failure, "Cannot inspect cache path");
		require(!fs::is_symlink(s), "Symlink in cache path");
#ifdef _WIN32
		auto attrs = GetFileAttributesW(prefix.c_str());
		require(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
			"Reparse point in cache path");
#endif
	}
}
Cache::Cache(fs::path p)
{
	p = fs::absolute(p).lexically_normal();
	no_links(p);
	fs::create_directories(p);
	root_ = fs::canonical(p);
	for (auto dir : {"locks", "blobs/sha256", "partial", "packages", "staging", "receipts", "licenses"})
		fs::create_directories(path(dir));
}
fs::path Cache::path(std::string_view relative) const
{
	relative_path(relative);
	auto p = root_ / fs::path(relative);
	no_links(p);
	return p;
}
fs::path Cache::blob(std::string_view digest) const
{
	require(hex_string(digest, 64), "Invalid blob digest");
	return path("blobs/sha256/" + std::string(digest));
}
static std::string key_path(const ModelKey &k)
{
	identifier(k.id);
	require(hex_string(k.revision, 40), "Invalid revision");
	return k.id + "/" + k.revision;
}
fs::path Cache::revision(const ModelKey &k) const
{
	return path("packages/" + key_path(k));
}
fs::path Cache::staging(const ModelKey &k) const
{
	return path("staging/" + key_path(k));
}
fs::path Cache::revision_lock(const ModelKey &k) const
{
	return path("locks/revision-" + sha256(key_path(k)) + ".lock");
}
void Cache::check_space(std::uint64_t bytes) const
{
	std::error_code ec;
	auto space = fs::space(root_, ec);
	require(!ec, "Cannot determine destination free space", ErrorCode::io_failure);
	require(bytes <= space.available, "Insufficient space on cache volume", ErrorCode::disk_full);
}
bool Cache::has_blob(const Artifact &a, std::stop_token stop) const
{
	auto p = blob(a.sha256);
	return fs::is_regular_file(p) && fs::file_size(p) == a.bytes && file_sha256(p, stop) == a.sha256;
}
void Cache::prepare(const Manifest &m)
{
	// Caller holds the revision lock. Clear only an uncommitted generation.
	auto stage = staging(m.key);
	if (fs::exists(stage)) {
		no_links(stage);
		for (auto &e : fs::recursive_directory_iterator(stage))
			no_links(e.path());
		fs::remove_all(stage);
	}
	fs::create_directories(stage);
	for (const auto &g : m.graphs)
		for (auto id : {g.graph, g.metadata}) {
			auto &a = m.artifacts.at(id);
			require(has_blob(a), "Missing authenticated graph artifact", ErrorCode::model_missing);
			auto target = stage / a.path;
			no_links(target);
			fs::create_directories(target.parent_path());
			fs::copy_file(blob(a.sha256), target);
		}
	atomic_write(stage / "manifest.json", m.raw);
}
void Cache::publish(const Manifest &m, std::stop_token stop)
{
	auto stage = staging(m.key);
	for (const auto &g : m.graphs) {
		auto &a = m.artifacts.at(g.graph);
		auto &meta = m.artifacts.at(g.metadata);
		for (const auto *entry : {&a, &meta}) {
			auto p = stage / entry->path;
			no_links(p);
			require(fs::file_size(p) == entry->bytes && file_sha256(p, stop) == entry->sha256,
				"Staged graph hash mismatch", ErrorCode::hash_mismatch);
		}
		validate_export_metadata(read_small_file(stage / meta.path, 32 * 1024 * 1024), g, m);
		auto p = stage / g.external_data;
		no_links(p);
		require(fs::file_size(p) == g.weights_bytes && file_sha256(p, stop) == g.weights_sha256,
			"Bound weights hash mismatch", ErrorCode::hash_mismatch);
	}
	for (auto &e : fs::recursive_directory_iterator(stage))
		no_links(e.path());
	require(!fs::exists(revision(m.key)), "Revision already exists", ErrorCode::busy);
	atomic_write(stage / "ready.json", Json{{"schema_version", 1}, {"manifest_sha256", m.digest}}.dump());
	fs::create_directories(revision(m.key).parent_path());
	atomic_finalize(stage, revision(m.key));
}
struct ReadLease final : ModelReadLease {
	FileLock data, revision;
	ReadLease(const fs::path &global, const fs::path &local)
		: data(global, true, false),
		  revision(local, true, false)
	{
	}
};
std::shared_ptr<const ReadyModel> Cache::acquire(const Manifest &m, std::stop_token stop) const
{
	auto lease = std::make_shared<ReadLease>(path("locks/data.lock"), revision_lock(m.key));
	if (!lease->data.owns_lock() || !lease->revision.owns_lock())
		return {};
	auto dir = revision(m.key);
	if (!fs::is_regular_file(dir / "ready.json"))
		return {};
	no_links(dir / "ready.json");
	auto r = strict_json(read_small_file(dir / "ready.json"));
	fields(r, {"schema_version", "manifest_sha256"});
	require(uint64(r.at("schema_version")) == 1 && string(r.at("manifest_sha256")) == m.digest,
		"Ready receipt mismatch", ErrorCode::hash_mismatch);
	no_links(dir / "manifest.json");
	require(file_sha256(dir / "manifest.json", stop) == m.digest, "Cached manifest changed",
		ErrorCode::hash_mismatch);
	auto ready = std::make_shared<ReadyModel>();
	ready->key = m.key;
	ready->manifest_sha256 = m.digest;
	ready->lease = lease;
	for (const auto &g : m.graphs) {
		auto &a = m.artifacts.at(g.graph);
		const auto &metadata = m.artifacts.at(g.metadata);
		auto p = dir / a.path, w = dir / g.external_data, meta = dir / metadata.path;
		no_links(p);
		no_links(w);
		no_links(meta);
		require(fs::file_size(p) == a.bytes && fs::file_size(w) == g.weights_bytes &&
				fs::file_size(meta) == metadata.bytes && file_sha256(meta, stop) == metadata.sha256 &&
				file_sha256(p, stop) == a.sha256 && file_sha256(w, stop) == g.weights_sha256,
			"Installed artifact changed", ErrorCode::hash_mismatch);
		ready->graphs.push_back({g.role, a.sha256, g.weights_sha256, "export-v1", p, w});
	}
	return ready;
}
bool Cache::remove(const ModelKey &key, bool remove_selected)
{
	FileLock selection(path("locks/selection.lock"), false, true);
	const bool selected = current(key.id) == std::optional<ModelKey>(key);
	if (selected && !remove_selected)
		return false;
	FileLock global(path("locks/data.lock"), true, false);
	if (!global.owns_lock())
		return false;
	FileLock lock(revision_lock(key), false, false);
	if (!lock.owns_lock())
		return false;
	auto p = revision(key);
	if (fs::exists(p)) {
		for (auto &e : fs::recursive_directory_iterator(p))
			no_links(e.path());
		fs::remove_all(p);
	}
	if (selected)
		fs::remove(path("receipts/current-" + key.id + ".json"));
	return true;
}
std::optional<ModelKey> Cache::current(std::string_view id) const
{
	identifier(id);
	auto p = path("receipts/current-" + std::string(id) + ".json");
	if (!fs::exists(p))
		return {};
	auto j = strict_json(read_small_file(p));
	fields(j, {"schema_version", "id", "revision"});
	require(uint64(j.at("schema_version")) == 1 && string(j.at("id")) == id &&
			hex_string(string(j.at("revision")), 40),
		"Invalid current revision");
	return ModelKey{string(j.at("id")), string(j.at("revision"))};
}
void Cache::select_current(const Manifest &m)
{
	FileLock selection(path("locks/selection.lock"), false, true);
	auto lease = acquire(m);
	require(bool(lease), "Cannot select an unready revision", ErrorCode::model_missing);
	atomic_write(path("receipts/current-" + m.key.id + ".json"),
		     Json{{"schema_version", 1}, {"id", m.key.id}, {"revision", m.key.revision}}.dump());
}
void Cache::clear_current(std::string_view id)
{
	identifier(id);
	FileLock selection(path("locks/selection.lock"), false, true);
	fs::remove(path("receipts/current-" + std::string(id) + ".json"));
}
bool Cache::prune_blobs(const std::set<std::string> &keep)
{
	FileLock lock(path("locks/data.lock"), false, false);
	if (!lock.owns_lock())
		return false;
	for (auto &e : fs::directory_iterator(path("blobs/sha256"))) {
		auto name = e.path().filename().string();
		require(hex_string(name, 64), "Unexpected blob name");
		no_links(e.path());
		if (!keep.contains(name))
			fs::remove(e.path());
	}
	return true;
}
} // namespace corridorkey::models
