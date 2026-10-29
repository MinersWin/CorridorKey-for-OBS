// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "util/utf8_path.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace corridorkey::ort {
using nlohmann::json;
namespace {
class Hash {
public:
	Hash()
	{
#ifdef _WIN32
		if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
		    BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) < 0)
			throw std::runtime_error("SHA-256 initialization failed");
#else
		hash_ = EVP_MD_CTX_new();
		if (!hash_ || EVP_DigestInit_ex(hash_, EVP_sha256(), nullptr) != 1)
			throw std::runtime_error("SHA-256 initialization failed");
#endif
	}
	~Hash()
	{
#ifdef _WIN32
		if (hash_)
			BCryptDestroyHash(hash_);
		if (algorithm_)
			BCryptCloseAlgorithmProvider(algorithm_, 0);
#else
		EVP_MD_CTX_free(hash_);
#endif
	}
	void add(const char *data, size_t size)
	{
#ifdef _WIN32
		if (BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char *>(data)), static_cast<ULONG>(size),
				   0) < 0)
#else
		if (EVP_DigestUpdate(hash_, data, size) != 1)
#endif
			throw std::runtime_error("SHA-256 update failed");
	}
	std::string finish()
	{
		std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
		if (BCryptFinishHash(hash_, bytes.data(), 32, 0) < 0)
#else
		unsigned size = 32;
		if (EVP_DigestFinal_ex(hash_, bytes.data(), &size) != 1)
#endif
			throw std::runtime_error("SHA-256 finalization failed");
		std::ostringstream out;
		for (auto b : bytes)
			out << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
		return out.str();
	}

private:
#ifdef _WIN32
	BCRYPT_ALG_HANDLE algorithm_{};
	BCRYPT_HASH_HANDLE hash_{};
#else
	EVP_MD_CTX *hash_{};
#endif
};
fs::path private_path(const fs::path &root, const std::string &relative)
{
	fs::path path = corridorkey::utf8_path(relative);
	if (path.empty() || path.is_absolute() || path.has_root_name())
		throw std::runtime_error("Bundle path must be relative");
	for (const auto &part : path)
		if (part == "..")
			throw std::runtime_error("Bundle path escapes private directory");
	const auto result = fs::canonical(root / path);
	const auto rel = result.lexically_relative(fs::canonical(root));
	if (rel.empty() || *rel.begin() == "..")
		throw std::runtime_error("Bundle symlink escapes private directory");
	return result;
}
} // namespace
std::string sha256(std::string_view data)
{
	Hash h;
	h.add(data.data(), data.size());
	return h.finish();
}
std::string file_sha256(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("Bundle file cannot be read");
	Hash hash;
	std::array<char, 65536> buffer;
	while (in.read(buffer.data(), buffer.size()) || in.gcount())
		hash.add(buffer.data(), static_cast<size_t>(in.gcount()));
	if (!in.eof())
		throw std::runtime_error("Bundle file read failed");
	return hash.finish();
}
std::string backend_name(Backend b)
{
	switch (b) {
	case Backend::automatic:
		return "auto";
	case Backend::tensorrt_rtx:
		return "tensorrt-rtx";
	case Backend::tensorrt:
		return "tensorrt";
	case Backend::cuda:
		return "cuda";
	case Backend::directml:
		return "directml";
	case Backend::migraphx:
		return "migraphx";
	case Backend::cpu_diagnostic:
		return "cpu";
	default:
		throw std::runtime_error("Unsupported backend");
	}
}
Backend parse_backend(std::string_view s)
{
	for (auto b : {Backend::automatic, Backend::tensorrt_rtx, Backend::tensorrt, Backend::cuda, Backend::directml,
		       Backend::migraphx, Backend::cpu_diagnostic})
		if (s == backend_name(b))
			return b;
	throw std::runtime_error("Unknown backend name");
}
Bundle read_bundle(const fs::path &descriptor)
{
	if (fs::file_size(descriptor) > 1024 * 1024)
		throw std::runtime_error("Bundle descriptor too large");
	std::ifstream in(descriptor);
	const auto j = json::parse(in);
	if (j.at("schema") != 1)
		throw std::runtime_error("Unsupported bundle schema");
	Bundle b;
	b.descriptor = fs::canonical(descriptor);
	b.root = fs::canonical(descriptor).parent_path();
	b.id = j.at("id");
	b.runtime_build = j.at("runtime_build");
	b.provider_build = j.at("provider_build");
	b.provider_name = j.at("provider_name");
	b.backend = parse_backend(j.at("backend").get<std::string>());
	const std::string registration = j.at("registration");
	if (registration == "plugin")
		b.registration = Registration::plugin;
	else if (registration == "classic")
		b.registration = Registration::classic;
	else if (registration == "cpu" && b.backend == Backend::cpu_diagnostic)
		b.registration = Registration::cpu;
	else
		throw std::runtime_error("Invalid provider registration mode");
	b.runtime = private_path(b.root, j.at("runtime"));
	if (j.contains("provider"))
		b.provider = private_path(b.root, j.at("provider"));
	if (b.registration == Registration::plugin && b.provider.empty())
		throw std::runtime_error("Plugin library missing");
	for (const auto &p : j.value("preload", json::array()))
		b.preload.push_back(private_path(b.root, p));
	b.file_hashes = j.at("files").get<decltype(b.file_hashes)>();
	b.options = j.value("options", decltype(b.options){});
	return b;
}
void verify_bundle(const Bundle &b)
{
	std::vector<fs::path> checked;
	for (const auto &[name, expected] : b.file_hashes) {
		const auto path = private_path(b.root, name);
		if (expected.size() != 64 || file_sha256(path) != expected)
			throw std::runtime_error("Runtime bundle SHA-256 mismatch");
		checked.push_back(path);
	}
	auto require = [&](const fs::path &p) {
		if (std::find(checked.begin(), checked.end(), p) == checked.end())
			throw std::runtime_error("Loaded library absent from bundle integrity manifest");
	};
	require(b.runtime);
	if (!b.provider.empty())
		require(b.provider);
	for (const auto &p : b.preload)
		require(p);
}
std::vector<Backend> candidates(Platform platform, std::uint32_t vendor, Backend requested)
{
	if (requested != Backend::automatic)
		return {requested};
	std::vector<Backend> list;
	if (vendor == 0x10de || vendor == 0)
		list = {Backend::tensorrt_rtx, Backend::tensorrt, Backend::cuda};
	if (platform == Platform::windows)
		list.push_back(Backend::directml);
	else if (vendor == 0x1002 || vendor == 0)
		list.push_back(Backend::migraphx);
	return list;
}
std::string canonical_adapter_id(std::string_view id)
{
	const std::string value(id);
	if (id.starts_with("pci:")) {
		unsigned domain = 0, bus = 0, device = 0, function = 0;
		int consumed = 0;
		if (std::sscanf(value.c_str() + 4, "%x:%x:%x.%x%n", &domain, &bus, &device, &function, &consumed) !=
			    4 ||
		    consumed != static_cast<int>(value.size() - 4) || bus > 255 || device > 31 || function > 7)
			return {};
		std::ostringstream out;
		out << "pci:" << std::hex << std::setfill('0') << std::setw(8) << domain << ':' << std::setw(2) << bus
		    << ':' << std::setw(2) << device << '.' << function;
		return out.str();
	}
	return value;
}
bool adapter_matches(const AdapterIdentity &a, const AdapterIdentity &b)
{
	const auto identity = canonical_adapter_id(a.stable_id);
	return !identity.empty() && identity == canonical_adapter_id(b.stable_id) &&
	       (!a.vendor_id || !b.vendor_id || a.vendor_id == b.vendor_id);
}
Selection select_provider(ProviderProbe &probe, Platform platform, ProbeRequest request, std::stop_token stop)
{
	Selection out;
	const bool diagnostic = request.backend == Backend::cpu_diagnostic;
	for (auto b : candidates(platform, request.obs_adapter.vendor_id, request.backend)) {
		if (stop.stop_requested())
			break;
		request.backend = b;
		auto result = probe.probe(request, stop);
		if (result.provider.backend != b) {
			result.failure = error(ErrorCode::backend_unavailable);
			result.diagnostic_detail =
				"Probe returned a different provider; trying the next permitted backend";
		}
		const auto &p = result.provider;
		const bool accepted = !result.failure && p.backend == b && p.available && p.tested &&
				      (diagnostic || (p.permits_live_inference && p.matches_obs_adapter));
		if (accepted)
			out.selected = p;
		out.attempts.push_back(std::move(result));
		if (accepted)
			break;
	}
	return out;
}
std::string CacheIdentity::canonical() const
{
	return json{{"schema", 1},
		    {"manifest", manifest_hash},
		    {"graph", graph_hash},
		    {"weights", weights_hash},
		    {"recipe", recipe_version},
		    {"runtime", runtime_build},
		    {"provider", provider_build},
		    {"driver", driver},
		    {"gpu", gpu},
		    {"architecture", architecture},
		    {"precision", precision},
		    {"shapes", shapes},
		    {"tiles", tile_batch},
		    {"options", options}}
		.dump();
}
} // namespace corridorkey::ort
