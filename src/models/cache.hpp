// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#pragma once
#include "manifest.hpp"
#include <functional>
#include <memory>

namespace corridorkey::models {
// OS byte-range/advisory locks survive neither a crash nor process exit.
// Lock files stay on disk; deleting them would split the lock namespace.
class FileLock {
	struct Impl;
	std::unique_ptr<Impl> impl;

public:
	FileLock() noexcept;
	FileLock(const fs::path &, bool shared, bool wait, std::stop_token = {});
	~FileLock();
	FileLock(FileLock &&) noexcept;
	FileLock &operator=(FileLock &&) noexcept;
	FileLock(const FileLock &) = delete;
	bool owns_lock() const noexcept;
};
std::string read_small_file(const fs::path &, std::size_t limit = 4 * 1024 * 1024);
void atomic_write(const fs::path &, std::string_view);
void atomic_finalize(const fs::path &partial, const fs::path &destination);
class Cache {
	fs::path root_;

public:
	// Pass the OBS-resolved path, or an explicit user-selected external location.
	explicit Cache(fs::path root);
	const fs::path &root() const noexcept { return root_; }
	fs::path path(std::string_view relative) const;
	fs::path blob(std::string_view digest) const;
	fs::path revision(const ModelKey &) const;
	fs::path staging(const ModelKey &) const;
	fs::path revision_lock(const ModelKey &) const;
	void check_space(std::uint64_t additional_bytes) const;
	bool has_blob(const Artifact &, std::stop_token = {}) const;
	void prepare(const Manifest &);
	void publish(const Manifest &, std::stop_token = {});
	std::shared_ptr<const ReadyModel> acquire(const Manifest &, std::stop_token = {}) const;
	bool remove(const ModelKey &); // false while leased or installing
	std::optional<ModelKey> current(std::string_view id) const;
	void select_current(const Manifest &); // explicit activation, never part of install
	void clear_current(std::string_view id);
	bool prune_blobs(const std::set<std::string> &keep); // false while any model is leased/installing
};
// Implemented only in the plugin target; owns/frees obs_module_config_path's result.
fs::path obs_model_cache_path();
} // namespace corridorkey::models
