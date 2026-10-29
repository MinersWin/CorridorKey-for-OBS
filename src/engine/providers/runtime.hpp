// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "contracts/capabilities.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <span>

namespace corridorkey::ort {
namespace fs = std::filesystem;
enum class Registration { plugin, classic, cpu };
enum class Platform { windows, linux_os };
struct Bundle {
	std::string id, runtime_build, provider_build, provider_name;
	Backend backend = Backend::automatic;
	Registration registration = Registration::plugin;
	fs::path descriptor, root, runtime, provider;
	std::vector<fs::path> preload;
	std::map<std::string, std::string> file_hashes, options;
};
// Descriptors are trusted installer configuration, not model-supplied metadata.
Bundle read_bundle(const fs::path &descriptor);
void verify_bundle(const Bundle &);
std::string sha256(std::string_view);
std::string file_sha256(const fs::path &);
std::string backend_name(Backend);
Backend parse_backend(std::string_view);
std::vector<Backend> candidates(Platform, std::uint32_t vendor, Backend requested);
bool adapter_matches(const AdapterIdentity &, const AdapterIdentity &);
std::string canonical_adapter_id(std::string_view);
struct Device {
	AdapterIdentity adapter;
	int ordinal = 0;
	std::string driver, architecture, pci_id;
};
std::vector<Device> enumerate_devices(Backend);
// All fields participate in cache identity, including weights and provider options.
struct CacheIdentity {
	std::string manifest_hash, graph_hash, weights_hash, recipe_version;
	std::string runtime_build, provider_build, driver, gpu, architecture, precision;
	std::vector<std::int64_t> shapes;
	std::uint32_t tile_batch = 0;
	std::map<std::string, std::string> options;
	std::string canonical() const;
};
class CacheLease {
public:
	CacheLease(const fs::path &root, const CacheIdentity &, bool rebuild = false);
	~CacheLease();
	CacheLease(const CacheLease &) = delete;
	CacheLease &operator=(const CacheLease &) = delete;
	const fs::path &path() const { return path_; }
	void commit(); // Call only after the EP has released its session and cache writers.
private:
	void lock();
	void unlock() noexcept;
	fs::path shared_, path_;
	std::intptr_t lock_ = -1;
};
struct ProbeConfig {
	fs::path helper, scratch, fixture_fp32, fixture_fp16, fixture_bf16;
	std::map<Backend, fs::path> bundles;
};
class DisposableProbe final : public ProviderProbe {
public:
	explicit DisposableProbe(ProbeConfig config) : config_(std::move(config)) {}
	ProbeResult probe(const ProbeRequest &, std::stop_token) override;

private:
	ProbeConfig config_;
};
struct Selection {
	std::optional<ProviderCapabilities> selected;
	std::vector<ProbeResult> attempts;
};
Selection select_provider(ProviderProbe &, Platform, ProbeRequest, std::stop_token);
} // namespace corridorkey::ort
